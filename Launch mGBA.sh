#!/usr/bin/env bash
# Linux launcher for mGBA Willow. Double-click the "mGBA Willow" desktop icon
# (run this script with --install-desktop once to add it) or run it from a terminal.
cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")" || { echo "Could not open the mGBA checkout directory."; exit 1; }
repo_root=$(pwd)

pause_on_failure() {
	# The desktop icon runs in a terminal that closes on exit; keep errors visible.
	if [[ -t 0 ]]; then
		echo
		read -r -p "Setup or launch did not finish. Press Enter to close, then double-click the launcher to retry. " _
	fi
	exit 1
}

case "${1:-}" in
--install-desktop) exec /bin/sh "$repo_root/tools/install-desktop.sh" ;;
--install-terminal-command)
	mkdir -p "$HOME/.local/bin"
	ln -sfn "$repo_root/tools/terminal/mgba" "$HOME/.local/bin/mgba"
	echo "Installed the mgba command in ~/.local/bin. Open a new terminal and run: mgba [ROM]"
	exit 0 ;;
--install-startup) exec /bin/sh "$repo_root/tools/install-startup.sh" ;;
--remove-startup) exec /bin/sh "$repo_root/tools/install-startup.sh" --remove ;;
esac

[[ -f $repo_root/tools/launch-linux.sh ]] || { echo "Missing tools/launch-linux.sh. Restore the launcher files and retry."; pause_on_failure; }
printf '\033]0;mGBA Willow - Game Library + Multi-Agent MCP\007'
echo "Opening the latest mGBA Willow build with the new game-card library."
echo "Choose your ROM folder once; Willow remembers it for future launches."
echo "The library scans subfolders and loads ZIP/7z games without manual extraction."
echo "Recognized games show their own title splash on the cartridge; artwork is cached after download."
echo "Select a game card and click Play, or double-click a card to start."
echo "Use File / Return to game library after saving your current game."
echo "Multi-agent MCP support remains available during gameplay."
echo "Changed emulator sources will be rebuilt automatically; the first run installs build packages with apt (sudo)."
echo "To add the double-click desktop icon and menu entry, run \"Launch mGBA.sh\" --install-desktop."
echo "To enable mgba in your terminal, run \"Launch mGBA.sh\" --install-terminal-command."
echo "The mGBA Willow system tray icon shows when the iPhone link is ready."
echo "To open mGBA Willow minimized at sign-in, run \"Launch mGBA.sh\" --install-startup."
echo "Sign-in launches use the last build; run --remove-startup to undo."
echo "To play from your iPhone, click Pair iPhone in the Willow window, then scan the QR code or use Copy Link to send the link to the phone."
echo "iPhone pairing needs Node.js 20+ and Tailscale; allow Willow to use Tailscale once with: sudo tailscale set --operator=\$USER"
echo "Tap Auto Play on the phone to let Codex play, or Stop Auto Play to stop the current run."
echo "Auto Play models marked Pi run in the Pi agent harness; install it with npm install -g @earendil-works/pi-coding-agent."
echo "Tap Settings on the phone for game speed, the Auto Play model, video, button size and keeping the screen awake."
echo "Settings on the PC is one page: Willow options first, then the emulator; use the sidebar or Ctrl+F to find a setting."
echo "The iPhone link uses Tailscale Funnel HTTPS; Tailscale handles the certificate."
echo "Auto video tries NVIDIA NVENC HEVC first, then H.264 (NVENC, then x264), with a PNG fallback."
echo "GBA games render on the GPU with OpenGL; Willow uses the software renderer while an AI agent has control."
echo "After opening a ROM, multiple agents can connect; button actions run one at a time."
echo "Connected agents can update the Objective box with the set_objective MCP tool."
echo "After an update, restart the MCP server so agents discover the latest tools."
echo "Linux does not lock the running app, so a rebuild can finish while mGBA is open; restart mGBA to use it."
echo
if [[ -f $repo_root/POKEMON_EMERALD_OBJECTIVES.md ]]; then
	echo "Pokemon Emerald source of truth: \"$repo_root/POKEMON_EMERALD_OBJECTIVES.md\""
	echo "Goal: become Champion, enter the Hall of Fame, and save."
	echo "Consult the checklist before gameplay and update it after verified milestones."
else
	echo "Pokemon Emerald objectives file is missing: \"$repo_root/POKEMON_EMERALD_OBJECTIVES.md\""
fi
echo
/bin/bash "$repo_root/tools/launch-linux.sh" "$@" || pause_on_failure
# Leave the launch messages on screen briefly when started from the desktop icon.
[[ -t 0 ]] && sleep 3
exit 0
