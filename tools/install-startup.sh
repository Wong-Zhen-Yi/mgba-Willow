#!/bin/sh
# Open Willow minimized at desktop sign-in (XDG autostart) so the iPhone link is always served.
#   install-startup.sh [--remove]
set -eu
tools_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(dirname -- "$tools_dir")
autostart_dir="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
entry="$autostart_dir/mgba-willow.desktop"

if [ "${1:-}" = "--remove" ]; then
	if [ -f "$entry" ]; then
		rm -f -- "$entry"
		echo "Willow no longer opens when you sign in."
	else
		echo "Willow was not set to open when you sign in."
	fi
	exit 0
fi

launcher="$tools_dir/launch-linux.sh"
[ -f "$launcher" ] || { echo "Launcher is missing: $launcher" >&2; exit 1; }
mkdir -p "$autostart_dir"
cat >"$entry" <<DESKTOP
[Desktop Entry]
Type=Application
Name=mGBA Willow
Comment=Open mGBA Willow minimized and serve Willow Mobile
Exec=/bin/bash "$launcher" --background
Path=$repo_root
Icon=$repo_root/res/mgba-256.png
Terminal=false
X-GNOME-Autostart-enabled=true
DESKTOP
echo "mGBA Willow will open minimized at sign-in. Check its tray icon for server status."
echo "To undo this, run \"Launch mGBA.sh\" --remove-startup."
