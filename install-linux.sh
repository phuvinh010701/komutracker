#!/bin/sh
# Build and install KomuTracker for the current user, with a launcher icon.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j"$(nproc)"
install -Dm755 build/komutracker ~/.local/bin/komutracker
convert_icon=~/.local/share/icons/hicolor/256x256/apps/komutracker.png
mkdir -p "$(dirname "$convert_icon")"
# logo.ico is a single 256x256 PNG-encoded icon: strip the 22-byte ICO header
tail -c +23 assets/logo.ico > "$convert_icon"
mkdir -p ~/.local/share/applications
cat > ~/.local/share/applications/komutracker.desktop <<D
[Desktop Entry]
Type=Application
Name=KomuTracker
Comment=AFK activity tracker
Exec=$HOME/.local/bin/komutracker
Icon=komutracker
Terminal=false
Categories=Utility;
D
update-desktop-database ~/.local/share/applications 2>/dev/null || true
gtk-update-icon-cache -f ~/.local/share/icons/hicolor 2>/dev/null || true
echo installed
