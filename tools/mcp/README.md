# Play mGBA with Luna in Codex

This local MCP controls a visible game in this checkout's Qt mGBA app. It provides screenshots, exact frame-counted button presses, read-only memory, and separate AI checkpoints. Games pause while the model thinks. Node.js 20 or newer is required.

## Setup

1. Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/mcp/setup.ps1` from the repository. This installs pinned dependencies and registers `mgba-willow` in your Codex MCP configuration, preserving other servers and model settings.
2. Open **Launch mGBA.cmd**, then open your GB, GBC, or GBA ROM. The launcher builds this checkout's bridge-enabled app when its source changes.
3. Restart or open a Codex chat so it discovers the MCP tools. Select **GPT-6 Luna**, **medium** reasoning in the model picker (another available Luna model can also use the tools).
4. Ask: **“Play the open game toward [goal], using checkpoints and stopping when you reach it.”**

The MCP itself never calls an inference API or changes your chat model. Gameplay uses your normal Codex model access and usage limits.

## Control and takeover

The agent uses `list_sessions`, `connect`, `observe`, and `act`. Connecting enables **AI → AI Control** and pauses the game. The status bar shows its connection. One client can control each window. Multiplayer sessions are unsupported.

Each action holds its buttons for 1–600 frames, releases them, and returns the resulting PNG. Empty buttons advance time without input. Keyboard input and autofire are suppressed while AI control is active. The game stays paused between actions.

Uncheck **AI → AI Control** to stop the agent and take over, then resume through the normal Pause control. A disconnect or timeout also releases buttons and leaves the game paused. Reconnect after closing or changing the game. No ROMs are downloaded or supplied by this integration.

## Memory and checkpoints

Call `memory_map` before `read_memory`. Readable regions expose numeric byte addresses, exclusive end bounds, and bank limits. `read_memory` returns hexadecimal bytes, at most 4096 per call. Omit `segment` or use `-1` for the current mapping. Reads cannot cross regions. MMIO, BIOS, and virtual regions are excluded; memory writes are unavailable. Do not treat arbitrary bytes as health or coordinates without verifying the exact game and version.

Checkpoint names use 1–64 letters, digits, underscores, or hyphens. `save_checkpoint` overwrites that named AI checkpoint; `load_checkpoint` restores it, including saved data. Files live under `%LOCALAPPDATA%/mgba-willow/ai/checkpoints/<ROM checksum>/`, outside normal save-state slots. Normal game saves may still be written as the game runs.

## Troubleshooting and tests

- No sessions: open this checkout's app with the launcher, load a ROM, then call `list_sessions` again. Other mGBA installations do not include the bridge.
- Already controlled: disconnect the other chat or toggle AI Control off in that window.
- Lost connection: reconnect; stale discovery files from crashed apps are ignored.
- Tools unavailable: run `codex mcp list`, rerun setup if needed, and restart the chat. Registration does not inject tools into an already running turn.
- Run `npm test` in `tools/mcp` for protocol tests. With a test ROM open, run `npm run smoke` for live bridge checks; this advances the game and creates an AI test checkpoint.

The bridge uses a Windows named pipe restricted to the current user. There is no network listener or public service.
