# CLAUDE.md

@AGENTS.md

The notes below add Claude-specific, Windows-specific details to AGENTS.md.

## Windows build loop

- `Launch mGBA.cmd` → `tools/launch-windows.ps1` hashes `src/`, `include/` and
  `res/`. It rebuilds into `build-auto-windows/` when any of them change, then
  starts `mGBA.exe` with `WILLOW_MOBILE_SCRIPT` and `WILLOW_AUTOPLAY_SCRIPT` set.
- Windows locks a running `mGBA.exe`, so the launcher refuses to rebuild while
  it runs. Close mGBA only when no game is open (check `mobile_session` →
  `started` over the bridge), or ask first. An open game may have unsaved
  progress.
- To compile without relinking (for example while mGBA runs), put MSYS2 on
  PATH and build single objects:
  ```sh
  MSYS="$LOCALAPPDATA/mgba-willow-tools/msys64"
  export PATH="$MSYS/ucrt64/bin:$MSYS/usr/bin:$PATH"
  cd build-auto-windows
  ninja qt/CMakeFiles/mgba-qt.dir/AIController.cpp.obj
  ```
- Qt unit tests live in the same build tree, for example
  `ninja qt/test-qt-gamelibrary.exe`, then run it with
  `QT_QPA_PLATFORM=offscreen ./qt/test-qt-gamelibrary.exe`.
- When behaviour or setup changes, update the user-facing echo lines in
  `Launch mGBA.cmd`. That file uses CRLF line endings, so keep them.
  Python's `read_text()` silently turns them into LF.

## Linux build loop

- `Launch mGBA.sh` → `tools/launch-linux.sh` installs the apt build packages
  once, hashes `src/`, `include/` and `res/`, rebuilds `build-auto-linux/`
  (shared libmgba, Qt 6) on change, runs `npm ci` in `tools/mcp` if needed,
  then starts `qt/mgba-qt` detached with the same `WILLOW_*` variables.
  Logs: `build-auto-linux/launcher.log` and `build-auto-linux/mgba.log`.
- `--install-desktop` writes `~/Desktop/mGBA Willow.desktop` and
  `~/.local/share/applications/mgba-willow.desktop` with absolute paths and
  marks them trusted for XFCE/GNOME; rerun it if the checkout moves.
- Linux does not lock a running binary, so rebuilding while mGBA runs is safe;
  restart mGBA to pick up the new build.
- The bridge uses a Unix socket in `$TMPDIR` and descriptors in
  `~/.local/share/mgba-willow/ai/sessions`. Tailscale Serve/Funnel needs
  `tailscale set --operator=$USER`.

## MCP server (`tools/mcp/`)

- `npm test` runs every `test/*.test.mjs` with `node:test`. Run it after any
  change under `tools/mcp/`.
- Each mGBA window writes a session descriptor and serves a named pipe
  (`bridge.mjs` → `Bridge`, `descriptors`). Stale descriptors from closed
  windows are normal; skip ones that fail with `ENOENT`.
- Auto Play profiles live in `autoplay.mjs` (`profiles`) and `AutoPlay.cpp`
  (`_models`); keep the ids in sync. `pi-*` profiles run `pi --mode json -p`
  with `pi-extension.mjs`, which wraps `server.mjs` as Pi tools and adds the
  `willow-codex` provider from `~/.codex/auth.json` (access token only). Pi's
  own `openai-codex` login on this machine is expired; don't rely on it.
- Phone-only commands are named `mobile_*` and are handled in
  `AIController::mobileRequest`. `mobile_session`, `mobile_library`,
  `mobile_artwork`, `mobile_launch` and `mobile_connect` work without an
  attached controller; the rest need one.

## Willow Mobile (iPhone pairing)

User docs: `doc/willow-mobile.md`. Moving parts:

- `MobilePairing.cpp` (Qt dialog) starts `node mobile-launch.mjs <pid>
  <tailscale.exe>` and reads JSON lines from it (`progress`, `error`, `ready`
  with `origin`, `link`, `access` and `qr`).
- `mobile-launch.mjs` chooses the access mode. It defaults to **Funnel**
  (public `ts.net` HTTPS on port 443, 8443 or 10000, whichever is free), then
  falls back to `tailnet` (Serve HTTPS on 9444), then `http` (numeric
  Tailscale IP). `WILLOW_MOBILE_ACCESS` and `WILLOW_MOBILE_PORT` override it.
  Serve or Funnel runs in the foreground, so the forward disappears when the
  script exits.
- The script stops when its **stdin closes**. To run it by hand, keep stdin
  open with a wrapper; a plain `node … &` exits at once.
- `mobile-server.mjs` is the loopback HTTP and WebSocket server. It checks the
  Host and Origin headers, requires the token in the link's `#pair=` fragment,
  allows one controller, and evicts unpaired sockets beyond 8.
- Video: the phone sends `{type:'video', codec:'h264'}` when WebCodecs works,
  and the server then loops `mobile_frame {codec:'h264', wait:true}`. Qt holds
  that request until `frameAvailable` and encodes with `MobileVideoEncoder`
  (`src/willow/MobileVideo.cpp`: NVENC → AMF → QSV → Media Foundation → x264, all through
  the linked FFmpeg). Binary WebSocket frames start with a type byte: 0 PNG,
  1 H.264 delta, 2 keyframe. `WILLOW_MOBILE_ENCODER=libx264` forces an encoder.
  `test-willow-mobilevideo` decodes every encoder found and checks the colours.
- The GBA OpenGL renderer (`hwaccelVideo`, default on) is swapped for software
  while `CoreController::aiControl()` is true (`aiControlChanged` →
  `Window::changeRenderer`), because only software frames are `atomic:true`.
- `mobile/` holds the phone UI. The game picker (the PC's ROM library) is the
  first page and the GBA screen with the gamepad is the second. The server
  reads these files once at startup, so restart pairing to see UI edits; that
  also creates a new token.
- On this machine Tailscale DNS is off (`CorpDNS: false`), so the PC can't
  resolve its own `ts.net` name. Test with
  `curl --resolve <host>:<port>:<ip>`, or connect by IP with `servername` set.
  Phones may also have Tailscale DNS off, which is why Funnel is the default.
- To test the UI in a viewport, serve a loopback copy with
  `createMobileServer({ origin: 'http://127.0.0.1:<port>', token, pid })` and
  open it with the preview tools. The Host check rejects plain loopback URLs
  sent to the real server.
