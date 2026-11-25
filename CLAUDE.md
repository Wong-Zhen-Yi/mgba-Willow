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

## MCP server (`tools/mcp/`)

- `npm test` runs every `test/*.test.mjs` with `node:test`. Run it after any
  change under `tools/mcp/`.
- Each mGBA window writes a session descriptor and serves a named pipe
  (`bridge.mjs` → `Bridge`, `descriptors`). Stale descriptors from closed
  windows are normal; skip ones that fail with `ENOENT`.
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
