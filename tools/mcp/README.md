# Play mGBA with Luna in Codex

This local MCP controls a visible game in this checkout's Qt mGBA app. It provides screenshots, exact frame-counted button presses, read-only memory, and separate AI checkpoints. Games keep running while the model thinks. Node.js 20 or newer is required.

## Setup

1. Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/mcp/setup.ps1` from the repository. This installs pinned dependencies and registers `mgba-willow` in your Codex MCP configuration, preserving other servers and model settings.
2. Open **Launch mGBA.cmd**, then open your GB, GBC, or GBA ROM. The launcher builds this checkout's bridge-enabled app when its source changes.
3. Restart or open a Codex chat so it discovers the MCP tools. Select **GPT-6 Luna**, **medium** reasoning in the model picker (another available Luna model can also use the tools).
4. Ask: **“Play the open game toward [goal], using checkpoints and stopping when you reach it.”**

The MCP itself never calls an inference API or changes your chat model. Gameplay uses your normal Codex model access and usage limits.

## Availability, shared controls, and speed

The app starts its local bridge automatically, even before you load a ROM. **AI → MCP Enabled** turns the bridge off or back on for that window. Disabling it cancels pending actions and closes clients; re-enable it and reconnect to resume AI control. Each new app launch enables MCP again. The status bar shows ready, connected, disabled, or an endpoint error. Codex starts the stdio MCP adapter when it loads the registered server; no Windows background service is needed.

The agent uses `list_sessions`, `connect`, `observe`, and `act`. Connecting enables **AI → AI Control** without changing a manual pause. One AI client can control each window; multiplayer sessions remain unsupported. Your keyboard, controller, and autofire continue working alongside AI input. Human directions override opposite AI directions, and other buttons combine. Turning AI Control off, disconnecting, or timing out releases only AI buttons, preserves human input and the current pause state, and restores ordinary speed settings. **MCP Enabled** can prevent clients from reconnecting entirely.

**AI → AI Speed** selects 1×, 2×, 4×, 8×, or Maximum. The initial default is **4×**; selections persist. `set_speed` accepts `multiplier: 1`, `2`, `4`, `8`, or `"maximum"` and changes the same setting. Existing fast-forward menu controls and shortcuts temporarily take priority. Actual speed depends on hardware. Faster emulation reduces action time; it does not change Codex's model or inference latency.

Each `act` holds AI buttons for 1–600 emulated frames, releases them, and returns a screenshot by default. Empty buttons mean wait; `screenshot: false` returns metadata without encoding an image. `act_sequence` accepts `actions: [{ buttons: ["Right"], frames: 30 }, { buttons: ["A"], frames: 2 }]`, with 1–32 steps and at most 600 frames in total. Steps transition directly on the emulation thread and return one final screenshot by default, or metadata only with `screenshot: false`. Results include `action_start_frame`, `action_end_frame`, and per-step boundaries in `steps`; the screenshot frame may be later than completion.

Use batches for predictable moves and fresh screenshots near hazards. Manual pause cancels remaining steps and returns `cancelled: true`, a reason, and only fully completed step boundaries. Disabling MCP or changing games also cancels pending moves. A cancellation does not return a successful overall end frame. Observations include `human_keys`, `ai_keys`, and `effective_keys`; `keys` remains the effective emulator mask. Session metadata includes `mcp_enabled`, `ai_speed`, `frame`, and `paused`.

Games continue while Codex thinks and when the app loses focus or is minimized during AI control. The normal Pause control works; resume manually before issuing another action. Reconnect after changing, resetting, or closing a game. No ROMs are downloaded or supplied by this integration.

## Memory and checkpoints

Call `memory_map` before `read_memory`. Readable regions expose numeric byte addresses, exclusive end bounds, and bank limits. `read_memory` returns hexadecimal bytes, at most 4096 per call. Omit `segment` or use `-1` for the current mapping. Reads cannot cross regions. MMIO, BIOS, and virtual regions are excluded; memory writes are unavailable. Do not treat arbitrary bytes as health or coordinates without verifying the exact game and version.

Checkpoint names use 1–64 letters, digits, underscores, or hyphens. `save_checkpoint` overwrites that named AI checkpoint; `load_checkpoint` restores it, including saved data. Files live under `%LOCALAPPDATA%/mgba-willow/ai/checkpoints/<ROM checksum>/`, outside normal save-state slots. Normal game saves may still be written as the game runs.

## Troubleshooting and tests

- No sessions: open this checkout's app with the launcher, load a ROM, then call `list_sessions` again. Other mGBA installations do not include the bridge.
- Already controlled: disconnect the other chat or toggle AI Control off in that window.
- Lost connection: reconnect; stale discovery files from crashed apps are ignored.
- Tools unavailable: run `codex mcp list`, rerun setup if needed, and restart the chat. Registration does not inject tools into an already running turn.
- Run `npm test` in `tools/mcp` for protocol tests. The C++ `platform-qt-aigameplay` test (with `BUILD_SUITE=ON`) checks directional priority and frame sequences. With a test ROM open, run `npm run smoke` for live bridge checks; this advances the game and creates an AI test checkpoint.

The bridge uses a Windows named pipe restricted to the current user. There is no network listener or public service.
