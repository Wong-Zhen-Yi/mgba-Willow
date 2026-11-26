#!/bin/sh
# Add a double-click "mGBA Willow" launcher to the desktop and the applications menu.
set -eu
tools_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(dirname -- "$tools_dir")
launcher="$repo_root/Launch mGBA.sh"
[ -f "$launcher" ] || { echo "Launcher is missing: $launcher" >&2; exit 1; }
chmod +x "$launcher" "$tools_dir/launch-linux.sh" "$tools_dir/terminal/mgba" 2>/dev/null || true

write_entry() {
	cat >"$1" <<DESKTOP
[Desktop Entry]
Type=Application
Version=1.0
Name=mGBA Willow
GenericName=Game Boy Advance Emulator
Comment=Game library, Willow Mobile and multi-agent MCP for mGBA
Exec=/bin/bash "$launcher" %f
Path=$repo_root
Icon=$repo_root/res/mgba-256.png
Terminal=true
Categories=Game;Emulator;
MimeType=application/x-gameboy-advance-rom;application/x-agb-rom;application/x-gba-rom;application/x-gameboy-rom;application/x-gameboy-color-rom;
Keywords=emulator;Nintendo;advance;gba;Game Boy Advance;Willow;
DESKTOP
	chmod +x "$1"
	# Mark the launcher as trusted so XFCE and GNOME run it on double-click.
	if command -v gio >/dev/null 2>&1; then
		gio set "$1" metadata::trusted true 2>/dev/null || true
		gio set "$1" metadata::xfce-exe-checksum "$(sha256sum "$1" | cut -d' ' -f1)" 2>/dev/null || true
	fi
}

apps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$apps_dir"
write_entry "$apps_dir/mgba-willow.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps_dir" 2>/dev/null || true
echo "Added mGBA Willow to the applications menu."

desktop_dir=$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")
if [ -d "$desktop_dir" ] && [ "$desktop_dir" != "$HOME" ]; then
	write_entry "$desktop_dir/mGBA Willow.desktop"
	echo "Added the mGBA Willow icon to $desktop_dir. Double-click it to build and open Willow."
fi
