#!/usr/bin/env bash
set -euo pipefail
photodeck_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
photodeck_apps=${XDG_DATA_HOME:-"$HOME/.local/share"}/applications
photodeck_icons=${XDG_DATA_HOME:-"$HOME/.local/share"}/icons/hicolor/scalable/apps
mkdir -p "$photodeck_apps" "$photodeck_icons"
cp "$photodeck_root/packaging/photodeck.svg" "$photodeck_icons/photodeck.svg"
# Desktop entry quoting protects spaces; '%' is escaped for the desktop field-code syntax.
photodeck_exec=${photodeck_root//%/%%}
photodeck_exec=${photodeck_exec//\\/\\\\}
photodeck_exec=${photodeck_exec//\"/\\\"}
photodeck_exec=${photodeck_exec//\$/\\\$}
photodeck_exec=${photodeck_exec//\`/\\\`}
cat > "$photodeck_apps/photodeck.desktop" <<ENTRY
[Desktop Entry]
Type=Application
Name=PhotoDeck
Comment=Yerel fotoğraf ve katman düzenleyici
Exec="$photodeck_exec/launch.sh" %F
Icon=photodeck
Terminal=false
Categories=Graphics;Photography;RasterGraphics;
MimeType=image/png;image/jpeg;image/webp;image/bmp;
StartupNotify=true
ENTRY
printf 'Uygulama menüsü kısayolu: %s\n' "$photodeck_apps/photodeck.desktop"
