#!/usr/bin/env bash
# Linux counterpart of tools/launch-windows.ps1, shared by "Launch mGBA.sh",
# the desktop entry and the mgba terminal command.
#
#   launch-linux.sh [--caller-directory] [--background] [--] [ROM or mGBA arguments...]
#
# Installs the Ubuntu/Debian build dependencies once, rebuilds
# build-auto-linux/ whenever src/, include/, res/ or the build recipe changes,
# then starts mgba-qt with the Willow MCP and phone scripts.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(dirname -- "$script_dir")
build_dir="$repo_root/build-auto-linux"
log_path="$build_dir/launcher.log"
app_path="$build_dir/qt/mgba-qt"
build_ready="$build_dir/build-ready"
packages_ready="$build_dir/packages-ready-v1"
use_caller_directory=0
background=0
caller_directory=$(pwd)

while (($#)); do
	case "$1" in
	--caller-directory) use_caller_directory=1 ;;
	--background) background=1 ;;
	--) shift; break ;;
	*) break ;;
	esac
	shift
done

# Desktop launches get a minimal PATH; include per-user installs such as
# ~/.local/bin/node, nvm, Volta and the npm global prefix used by Pi.
for dir in "$HOME/.local/bin" "$HOME/.volta/bin" "$HOME/.npm-global/bin" "$HOME/bin"; do
	[[ -d $dir && :$PATH: != *:$dir:* ]] && PATH="$dir:$PATH"
done
if [[ -d $HOME/.nvm/versions/node ]] && ! command -v node >/dev/null; then
	latest=$(ls -1 "$HOME/.nvm/versions/node" | sort -V | tail -n1)
	[[ -n $latest ]] && PATH="$HOME/.nvm/versions/node/$latest/bin:$PATH"
fi
export PATH

mkdir -p "$build_dir"
exec 9>"$build_dir/launcher.lock"
if ! flock -n 9; then
	echo "Another launcher is already setting up this checkout. Please wait for that window to finish." >&2
	exit 1
fi
exec > >(exec 9>&-; tee "$log_path") 2>&1

fail() {
	echo
	echo "mGBA could not start: $*" >&2
	echo "Details are saved in: $log_path" >&2
	exit 1
}

# Packages for Ubuntu 24.04+ and Debian 13+ (Qt 6).
apt_packages=(
	build-essential cmake ninja-build pkg-config git
	qt6-base-dev qt6-multimedia-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools
	libgl-dev libsdl2-dev libepoxy-dev libzip-dev libpng-dev libsqlite3-dev liblua5.4-dev
	libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libavfilter-dev
	libelf-dev zlib1g-dev libedit-dev libfreetype-dev
)

as_root() {
	if [[ $EUID -eq 0 ]]; then
		"$@"
	elif sudo -n true 2>/dev/null; then
		sudo -n "$@"
	elif [[ -t 0 ]]; then
		sudo "$@"
	elif command -v pkexec >/dev/null && [[ -n ${DISPLAY:-}${WAYLAND_DISPLAY:-} ]]; then
		pkexec "$@"
	else
		fail "Installing build packages needs administrator rights. Run: sudo apt-get install ${apt_packages[*]}"
	fi
}

install_build_tools() {
	local missing=()
	if [[ -f $packages_ready ]] && command -v cmake >/dev/null && command -v ninja >/dev/null; then
		return
	fi
	if ! command -v dpkg-query >/dev/null || ! command -v apt-get >/dev/null; then
		for tool in cmake ninja c++ pkg-config; do
			command -v "$tool" >/dev/null || fail "This launcher installs packages with apt. Install cmake, ninja, a C++ compiler and the Qt 6, SDL2 and FFmpeg development packages, then retry."
		done
		touch "$packages_ready"
		return
	fi
	for pkg in "${apt_packages[@]}"; do
		dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q 'ok installed' || missing+=("$pkg")
	done
	if ((${#missing[@]})); then
		echo "[1/4] Installing the compiler and app dependencies: ${missing[*]}"
		echo "This first-time download can take a while."
		as_root env DEBIAN_FRONTEND=noninteractive apt-get update
		as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y "${missing[@]}"
	fi
	touch "$packages_ready"
}

# Hash the build inputs so changed emulator sources trigger a rebuild.
build_recipe() {
	(
		cd "$repo_root"
		{
			find src include res -type f -print0
			printf '%s\0' CMakeLists.txt version.cmake tools/launch-linux.sh
		} | LC_ALL=C sort -z | xargs -0 sha256sum
	) | sha256sum | cut -d' ' -f1
}

recipe=""
if ((background)) && [[ -x $app_path && -f $build_ready ]]; then
	# Sign-in launch: one window serves the phone, and the source hash is skipped.
	if pgrep -f "^$app_path( |$)" >/dev/null; then
		echo "mGBA is already running."
		exit 0
	fi
else
	recipe=$(build_recipe)
	if [[ ! -x $app_path || $(cat "$build_ready" 2>/dev/null) != "$recipe" ]]; then
		echo "Setting up mGBA automatically. Keep this window open until the app starts."
		install_build_tools
		echo "[2/4] Configuring the build..."
		cmake -S "$repo_root" -B "$build_dir" -G Ninja \
			-DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
			-DBUILD_QT=ON -DBUILD_SDL=ON -DFORCE_QT_VERSION=6 \
			-DBUILD_SHARED=ON -DBUILD_STATIC=OFF || fail "CMake configuration failed. See the details above."
		echo "[3/4] Building mGBA from this checkout..."
		jobs=$(nproc)
		((jobs > 8)) && jobs=8
		cmake --build "$build_dir" --target mgba-qt --parallel "$jobs" || fail "The build failed. See the details above."
		[[ -x $app_path ]] || fail "The build finished without producing the Linux app."
	fi
fi

# The MCP server and phone pairing need their Node packages.
mcp_dir="$script_dir/mcp"
if command -v npm >/dev/null && [[ ! -d $mcp_dir/node_modules ]]; then
	echo "Installing the MCP server packages..."
	(cd "$mcp_dir" && npm ci --no-audit --no-fund) || echo "npm ci failed; MCP and phone pairing may be unavailable."
fi
command -v node >/dev/null || echo "Node.js 20+ was not found; install it for MCP agents and iPhone pairing."
command -v tailscale >/dev/null || echo "Tailscale was not found; install it to play from your iPhone."

echo "[4/4] Opening mGBA with MCP objective updates..."
echo "Connected agents can use set_objective to update the Objective box."
echo "Restart the MCP server after updating this checkout to discover new tools."
export WILLOW_MOBILE_SCRIPT="$mcp_dir/mobile-launch.mjs"
export WILLOW_AUTOPLAY_SCRIPT="$mcp_dir/autoplay.mjs"
((background)) && export WILLOW_START_MINIMIZED=1
if ((use_caller_directory)); then cd "$caller_directory"; else cd "$build_dir/qt"; fi

app_log="$build_dir/mgba.log"
# Close the lock (fd 9) so the running app does not block later launches.
setsid "$app_path" "$@" </dev/null >"$app_log" 2>&1 9>&- &
app_pid=$!
sleep 2
if ! kill -0 "$app_pid" 2>/dev/null; then
	status=0
	wait "$app_pid" || status=$?
	if ((status != 0 || $# == 0)); then
		tail -n 20 "$app_log" >&2 || true
		fail "mGBA stopped during startup (exit code $status)."
	fi
fi
[[ -n $recipe ]] && printf '%s\n' "$recipe" >"$build_ready"
echo "mGBA is running (pid $app_pid). App output: $app_log"
