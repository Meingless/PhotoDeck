#!/usr/bin/env python3
"""Drive an isolated native PhotoDeck window using OS keyboard events.

Linux requires python-xlib and an X11/Xvfb display. macOS requires Accessibility
permission for the runner; failure is reported, never counted as a passing test.
The application is started with --no-settings and only temporary projects change.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time


def wait_for(check, label, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.1)
    raise RuntimeError(f"Timed out: {label}")


def layer_count(path):
    try:
        header = struct.unpack(">6I", path.read_bytes()[:24])
        return header[5] if header[0] == 0x50444543 else None
    except (OSError, struct.error):
        return None


class Keyboard:
    def __init__(self, pid):
        self.pid = pid
        if sys.platform.startswith("linux"):
            from Xlib import X, XK, display
            from Xlib.ext import xtest
            self.x, self.xk, self.xtest = X, XK, xtest
            self.display = display.Display()
            atom = self.display.intern_atom("_NET_WM_PID")

            def find(window, depth=0):
                try:
                    prop = window.get_full_property(atom, X.AnyPropertyType)
                    if prop is not None and len(prop.value) and prop.value[0] == pid:
                        geometry = window.get_geometry()
                        if window.get_attributes().map_state == X.IsViewable and geometry.width > 100 and geometry.height > 100:
                            return window
                    if depth < 3:
                        for child in window.query_tree().children:
                            found = find(child, depth + 1)
                            if found is not None:
                                return found
                except Exception:
                    return None
                return None

            self.window = wait_for(lambda: find(self.display.screen().root), "native X11 window")
        elif sys.platform == "win32":
            from ctypes import wintypes
            self.user = ctypes.WinDLL("user32", use_last_error=True)
            self.user.SetForegroundWindow.argtypes = [wintypes.HWND]
            self.user.IsWindowVisible.argtypes = [wintypes.HWND]
            self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            self.user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
            callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

            def find():
                matches = []

                @callback_type
                def visit(handle, _):
                    owner = wintypes.DWORD()
                    self.user.GetWindowThreadProcessId(handle, ctypes.byref(owner))
                    if owner.value == pid and self.user.IsWindowVisible(handle):
                        # Native tablet/IME services may own visible helper
                        # windows in this process. Select the editor window.
                        caption = ctypes.create_unicode_buffer(4096)
                        rect = wintypes.RECT()
                        self.user.GetWindowTextW(handle, caption, len(caption))
                        self.user.GetWindowRect(handle, ctypes.byref(rect))
                        if "PhotoDeck" in caption.value and rect.right - rect.left > 100 and rect.bottom - rect.top > 100:
                            matches.append(handle)
                    return True

                self.user.EnumWindows(visit, 0)
                return matches[0] if matches else None

            self.window = wait_for(find, "native Windows editor window", timeout=60)
        elif sys.platform == "darwin":
            time.sleep(1)
        else:
            raise RuntimeError(f"Unsupported keyboard backend: {sys.platform}")
        self.focus()

    def title(self):
        if sys.platform.startswith("linux"):
            return self.window.get_wm_name() or ""
        if sys.platform == "win32":
            from ctypes import wintypes
            self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            value = ctypes.create_unicode_buffer(4096)
            self.user.GetWindowTextW(self.window, value, len(value))
            return value.value
        result = subprocess.run(["osascript", "-e", f'tell application "System Events" to get name of front window of first process whose unix id is {self.pid}'], check=True, capture_output=True, text=True)
        return result.stdout.strip()

    def focus(self):
        if sys.platform.startswith("linux"):
            from Xlib.protocol import event
            root = self.display.screen().root
            root.send_event(event.ClientMessage(
                window=self.window,
                client_type=self.display.intern_atom("_NET_ACTIVE_WINDOW"),
                data=(32, [2, self.x.CurrentTime, 0, 0, 0]),
            ), event_mask=self.x.SubstructureRedirectMask | self.x.SubstructureNotifyMask)
            self.display.sync()
            time.sleep(0.1)
            self.window.configure(stack_mode=self.x.Above)
            self.window.set_input_focus(self.x.RevertToParent, self.x.CurrentTime)
            self.display.sync()
        elif sys.platform == "win32":
            self.user.SetForegroundWindow(self.window)
        else:
            subprocess.run(["osascript", "-e", f'tell application "System Events" to set frontmost of first process whose unix id is {self.pid} to true'], check=True, capture_output=True)
        time.sleep(0.2)

    def shortcut(self, key, shift=False):
        self.focus()
        if sys.platform.startswith("linux"):
            names = ["Control_L"] + (["Shift_L"] if shift else []) + [key]
            codes = [self.display.keysym_to_keycode(self.xk.string_to_keysym(name)) for name in names]
            for code in codes:
                self.xtest.fake_input(self.display, self.x.KeyPress, code)
                self.display.sync()
                time.sleep(0.05)
            for code in reversed(codes):
                self.xtest.fake_input(self.display, self.x.KeyRelease, code)
                self.display.sync()
                time.sleep(0.05)
            self.display.sync()
        elif sys.platform == "win32":
            codes = [0x11] + ([0x10] if shift else []) + [ord(key.upper())]
            for code in codes:
                self.user.keybd_event(code, 0, 0, 0)
                time.sleep(0.05)
            for code in reversed(codes):
                self.user.keybd_event(code, 0, 2, 0)
                time.sleep(0.05)
        else:
            modifiers = "command down, shift down" if shift else "command down"
            subprocess.run(["osascript", "-e", f'tell application "System Events" to keystroke "{key}" using {{{modifiers}}}'], check=True, capture_output=True)
        time.sleep(0.25)

    def paint_linux_stroke(self):
        """Exercise the ordinary mouse brush path, not an application test API."""
        self.focus()
        code = self.display.keysym_to_keycode(self.xk.string_to_keysym("b"))
        self.xtest.fake_input(self.display, self.x.KeyPress, code)
        self.display.sync()
        time.sleep(0.05)
        self.xtest.fake_input(self.display, self.x.KeyRelease, code)
        self.display.sync()
        time.sleep(0.2)
        origin = self.display.screen().root.translate_coords(self.window, 0, 0)
        geometry = self.window.get_geometry()
        x = origin.x + int(geometry.width * 0.4)
        y = origin.y + int(geometry.height * 0.5)
        self.xtest.fake_input(self.display, self.x.MotionNotify, x=x, y=y)
        self.display.sync()
        time.sleep(0.1)
        self.xtest.fake_input(self.display, self.x.ButtonPress, 1)
        self.display.sync()
        for offset in [0, 15, 30, 45]:
            time.sleep(0.1)
            self.xtest.fake_input(self.display, self.x.MotionNotify, x=x + offset, y=y + offset // 3)
            self.display.sync()
        self.xtest.fake_input(self.display, self.x.ButtonRelease, 1)
        self.display.sync()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    proof = {"platform": sys.platform, "binary": str(binary), "checks": [], "passed": False}
    environment = os.environ.copy()
    if sys.platform.startswith("linux"):
        environment.pop("WAYLAND_DISPLAY", None)
        environment.pop("WAYLAND_SOCKET", None)
    process = None
    keyboard = None
    try:
        with tempfile.TemporaryDirectory(prefix="photodeck-ui-", dir=args.output) as tmp:
            project = Path(tmp) / "native-smoke.pdeck"
            subprocess.run([binary, "--write-demo", project], check=True, env=environment, timeout=30)
            original = project.read_bytes()
            assert layer_count(project) == 4
            with (args.output / "native-ui.log").open("w") as log:
                process = subprocess.Popen([binary, "--no-settings", "--window-size", "1024", "768", project], env=environment, stdout=log, stderr=log)
                keyboard = Keyboard(process.pid)
                proof["initial_window_title"] = keyboard.title()
                # Cold system-font discovery and Mesa initialization on a
                # hosted VM can take longer than an ordinary edit/save action.
                wait_for(lambda: project.name in keyboard.title(), "document window is initialized", timeout=60)
                time.sleep(1)
                for label, key, shift, expected in [
                    ("keyboard add layer and save", "n", True, 5),
                    ("keyboard undo and save", "z", False, 4),
                    ("keyboard redo and save", "z", True, 5),
                ]:
                    keyboard.shortcut(key, shift)
                    wait_for(lambda: "*" in keyboard.title(), f"{label}: edit marked dirty")
                    keyboard.shortcut("s")
                    wait_for(lambda: layer_count(project) == expected, label)
                    wait_for(lambda: "*" not in keyboard.title(), f"{label}: saved state shown")
                    proof["checks"].append({"action": label, "layers": expected})
                    if label == "keyboard add layer and save" and sys.platform.startswith("linux"):
                        blank_layer = project.read_bytes()
                        keyboard.paint_linux_stroke()
                        wait_for(lambda: "*" in keyboard.title(), "mouse brush marks document dirty")
                        keyboard.shortcut("s")
                        wait_for(lambda: "*" not in keyboard.title(), "painted document saved")
                        assert project.read_bytes() != blank_layer, "Mouse painting must change persisted pixels"
                        proof["checks"].append({"action": "OS mouse brush stroke and save", "layers": 5})
                        keyboard.shortcut("z")
                        wait_for(lambda: "*" in keyboard.title(), "undo mouse brush")
                        keyboard.shortcut("s")
                        wait_for(lambda: project.read_bytes() == blank_layer, "undo restores exact pre-stroke bytes")
                        wait_for(lambda: "*" not in keyboard.title(), "undo mouse stroke saved")
                        proof["checks"].append({"action": "undo stroke restores exact file", "layers": 5})
                    if expected == 4:
                        assert project.read_bytes() == original, "Undo must restore the saved document bytes"
                proof["saved_sha256"] = hashlib.sha256(project.read_bytes()).hexdigest()
                # Close the clean document using its ordinary shortcut. Terminate
                # the remaining empty application below; no user work is involved.
                keyboard.shortcut("w")
                process.terminate()
                process.wait(timeout=10)
                process = None
            screenshot = (args.output / "reopened.png").resolve()
            subprocess.run([binary, "--no-settings", "--window-size", "1024", "768", "--screenshot", screenshot, project], check=True, env=environment, timeout=40)
            assert screenshot.stat().st_size > 1000
            proof["checks"].append({"action": "native reopen and screenshot", "layers": 5})
            proof["passed"] = True
    except Exception as error:
        proof["error"] = str(error)
        if keyboard is not None:
            try:
                proof["last_window_title"] = keyboard.title()
            except Exception as title_error:
                proof["window_title_error"] = str(title_error)
        if process is not None:
            proof["process_exit_code"] = process.poll()
        raise
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        (args.output / "native-ui-proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps(proof, indent=2))


if __name__ == "__main__":
    main()
