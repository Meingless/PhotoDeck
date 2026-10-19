"""Standard-library checks for checked-in icon assets and native package wiring."""
from pathlib import Path
import plistlib
import struct
import unittest
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parents[1]
PACKAGING = ROOT / "packaging"
SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)


def png_size(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    compressed = bytearray()
    size = None
    while offset < len(data):
        length, = struct.unpack_from(">I", data, offset)
        kind = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + length]
        crc, = struct.unpack_from(">I", data, offset + 8 + length)
        assert zlib.crc32(kind + chunk) == crc, "PNG chunk CRC mismatch"
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", chunk)
            assert (depth, color, compression, filtering, interlace) == (8, 6, 0, 0, 0)
            size = (width, height)
        elif kind == b"IDAT":
            compressed.extend(chunk)
        offset += 12 + length
        if kind == b"IEND":
            break
    assert offset == len(data), "Unexpected trailing PNG data"
    assert size is not None
    assert len(zlib.decompress(compressed)) == size[1] * (1 + size[0] * 4)
    return size


class IconAssetTests(unittest.TestCase):
    def png(self, size):
        return (PACKAGING / f"icons/photodeck-{size}.png").read_bytes()

    def test_all_png_sizes_are_rgba_and_intact(self):
        for size in SIZES:
            with self.subTest(size=size):
                self.assertEqual(png_size(self.png(size)), (size, size))
        self.assertEqual((PACKAGING / "photodeck.png").read_bytes(), self.png(512))

    def test_svg_master_is_self_contained_vector(self):
        svg = ET.parse(PACKAGING / "photodeck.svg").getroot()
        ns = {"s": "http://www.w3.org/2000/svg"}
        self.assertEqual(svg.get("viewBox"), "0 0 1024 1024")
        self.assertEqual(len(svg.findall(".//s:path", ns)), 3)
        self.assertFalse(svg.findall(".//s:image", ns))
        self.assertFalse(svg.findall(".//s:filter", ns))
        for element in svg.iter():
            self.assertFalse(any(key.endswith("href") for key in element.attrib))

    def test_windows_ico_contains_exact_small_representations(self):
        data = (PACKAGING / "photodeck.ico").read_bytes()
        self.assertEqual(struct.unpack_from("<HHH", data), (0, 1, 7))
        found = set()
        for index in range(7):
            width, height, _, _, _, depth, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + index * 16)
            width, height = width or 256, height or 256
            self.assertEqual(depth, 32)
            self.assertEqual(png_size(data[offset:offset + length]), (width, height))
            self.assertEqual(data[offset:offset + length], self.png(width))
            found.add(width)
        self.assertEqual(found, {16, 24, 32, 48, 64, 128, 256})

    def test_macos_icns_contains_native_and_retina_sizes(self):
        data = (PACKAGING / "photodeck.icns").read_bytes()
        self.assertEqual(data[:4], b"icns")
        self.assertEqual(struct.unpack_from(">I", data, 4)[0], len(data))
        expected = {b"icp4": 16, b"icp5": 32, b"icp6": 64, b"ic07": 128,
                    b"ic08": 256, b"ic09": 512, b"ic10": 1024,
                    b"ic11": 32, b"ic12": 64, b"ic13": 256, b"ic14": 512}
        offset = 8
        found = {}
        while offset < len(data):
            kind, length = struct.unpack_from(">4sI", data, offset)
            self.assertGreater(length, 8)
            png = data[offset + 8:offset + length]
            self.assertEqual(png_size(png), (expected[kind], expected[kind]))
            self.assertEqual(png, self.png(expected[kind]))
            self.assertNotIn(kind, found)
            found[kind] = expected[kind]
            offset += length
        self.assertEqual(offset, len(data))
        self.assertEqual(found, expected)

    def test_qt_resources_include_exact_small_sizes(self):
        files = {element.get("alias"): element.text
                 for element in ET.parse(ROOT / "resources.qrc").findall(".//file")}
        for path in files.values():
            self.assertTrue((ROOT / path).is_file(), path)
        for size in SIZES[:7]:
            self.assertEqual(files[f"icons/photodeck-{size}.png"], f"packaging/icons/photodeck-{size}.png")
        self.assertEqual(files["photodeck.png"], "packaging/photodeck.png")

    def test_native_bundle_and_build_references(self):
        info = plistlib.loads((PACKAGING / "Info.plist").read_bytes())
        self.assertEqual(info["CFBundleIconFile"], "photodeck.icns")
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("packaging/photodeck.rc.in", cmake)
        self.assertIn("target_sources(photodeck PRIVATE packaging/photodeck.icns)", cmake)
        self.assertIn("MACOSX_PACKAGE_LOCATION Resources", cmake)
        self.assertIn("1 ICON", (PACKAGING / "photodeck.rc.in").read_text())
        qmake = (ROOT / "PhotoDeck.pro").read_text()
        self.assertIn("macx:ICON = packaging/photodeck.icns", qmake)
        self.assertIn("win32:RC_ICONS = packaging/photodeck.ico", qmake)
        self.assertIn("packaging/photodeck.png", (ROOT / "scripts/package-appimage.py").read_text())
        self.assertIn("packaging/photodeck.svg", (ROOT / "scripts/install-user.sh").read_text())


if __name__ == "__main__":
    unittest.main()
