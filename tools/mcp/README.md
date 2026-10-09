# Play mGBA with Luna in Codex

This local MCP controls a visible game in this checkout's Qt mGBA app. It provides screenshots, exact frame-counted button presses, read-only memory, and separate AI checkpoints. Games keep running while the model thinks. Node.js 20 or newer is required.

## Structured Emerald state and navigation

The read-only `emerald_en_v1` adapter supports the unmodified English Pokémon Emerald retail ROM with SHA-1 `f3ae088181bf583e55daf962a92bb46f4f1d07b7`. It hashes the loaded ROM backing store, so a matching header alone or a patched ROM does not enable it. Other ROMs report `game_state.supported: false` and retain every raw tool. No ROM is supplied or downloaded.

- `get_game_state({full_state:true})` returns the current map group/number/ID, local tile coordinates, facing, movement and interaction state, battle action-menu readiness, party HP/status/moves/PP, bag item IDs/quantities, and numeric set event flags. Pokémon secure data is decrypted and checksum checked; inventory quantities use the captured save encryption key. Names and quest interpretations are not inferred from numeric IDs.
- `get_local_map({radius:4})` returns a square of nearby live collision tiles, elevations, behavior IDs, NPC positions, ladders, one-way ledges, warp destinations and map connections. Radius is 1–16. Coordinates omit Emerald's seven-tile internal border. Out-of-map or invalid backing pointers produce unknown tiles rather than walkable tiles.
- `move_to({x:12,y:8})` plans within 16 tiles of the starting position on the current map. It rechecks each next step and drives input on the emulation thread, releasing directions as soon as a tile step starts. It stops at battle, dialogue, script locks, map changes, unexpected movement, human input, new obstacles, 60 frames without progress, or `max_frames` (1–600, default 600). Inspect `progress.reason` and `progress.completed` before continuing. A no-path result advances no frames. The initial planner supports on-foot movement over ordinary tiles and elevation-compatible steps; it exposes but avoids ledges, surfing, bikes, bridges, ice puzzles, forced movement and other special traversal. Exits may be destinations but are not intermediate route tiles.
- `press({button:"A"})` holds for one frame, then explicitly releases for two frames. `hold_frames` and `release_frames` each accept 1–60. This tool also works on unsupported ROMs.
- `wait_until({condition:"battle_menu_ready"})` sends no AI buttons and evaluates readiness after every frame. Other conditions are `overworld_ready`, `map_transition_complete`, and `dialogue`. `stable_frames` defaults to 2 (1–60); `max_frames` defaults to 600 (1–600). `map_transition_complete` means the overworld is ready and does not require a transition to begin. Human input and manual pause interrupt the wait.

New tools return metadata without screenshots by default; request `screenshot:true` when needed. All observations and action results include decoded state. The first response has `state_format:"full"` and `game_state`. Later responses use `state_format:"delta"`, `base_frame`, `game_state_changes`, and `removed_state_fields`: replace each changed top-level field and remove the listed fields. `full_state:true` refreshes the baseline. Each MCP client has its own baseline, which resets on connect, checkpoint restore, map changes and backward frame changes. `observe` retains its screenshot default and accepts `screenshot:false`.

RAM is copied at completed frame boundaries during AI control; dynamic pointers are resolved exclusively within that copy. With software rendering, `atomic:true` certifies that screenshot and state share the indicated frame. Hardware rendering reports `atomic:false` because a matching completed framebuffer is not guaranteed. Immediately after connecting or restoring a checkpoint, state may be unavailable until a frame completes, including while manually paused. The game continues after an observation, so use fresh state before deciding. A route's `progress.stop_frame` is its stop frame; the returned observation may be later.

Raw directional actions additionally report `progress.position_unchanged`, `unchanged_attempts`, and `repeated_movement_loop` after three consecutive unchanged attempts from the same tile with the same directional mask. This indicates a reason to replan; a short turning animation can also leave coordinates unchanged.

Layout facts are checked against [pret/pokeemerald](https://github.com/pret/pokeemerald/tree/731ad5bfd6e6f265508d0efcca0ba42f9dcf5881) and its [matching symbol table](https://github.com/pret/pokeemerald/blob/dba968c67d85caf9595abe12a51ff739d4dc5937/pokeemerald.sym). Decoder and movement tests use synthetic RAM fixtures and do not need a ROM.

## Setup

1. Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/mcp/setup.ps1` from the repository. This installs pinned dependencies and registers `mgba-willow` in your Codex MCP configuration, preserving other servers and model settings.
2. Open **Launch mGBA.cmd**, then open your GB, GBC, or GBA ROM. The launcher builds this checkout's bridge-enabled app when its source changes.
3. Restart or open a Codex chat so it discovers the MCP tools. Select **GPT-6 Luna**, **medium** reasoning in the model picker (another available Luna model can also use the tools).
4. Ask: **“Play the open game toward [goal], using checkpoints and stopping when you reach it.”**

The MCP itself never calls an inference API or changes your chat model. Gameplay uses your normal Codex model access and usage limits.

## Availability, shared controls, and speed

The app starts its local bridge automatically, even before you load a ROM. **AI → MCP Enabled** turns the bridge off or back on for that window. Disabling it cancels pending actions and closes clients; re-enable it and reconnect to resume AI control. Each new app launch enables MCP again. The status bar shows ready, connected, disabled, or an endpoint error. Codex starts the stdio MCP adapter when it loads the registered server; no Windows background service is needed.

The agent uses `list_sessions`, `connect`, `observe`, and `act`. Connecting enables **AI → AI Control** without changing a manual pause. Multiple AI clients can connect to each window; multiplayer game sessions remain unsupported. Session metadata includes `connected_clients`. Actions run one at a time: an overlapping action, speed change, or checkpoint operation returns a busy error; observe and retry after completion. Other clients can connect, observe, and read memory while an action runs. All agents share game progress, speed, and checkpoints, so coordinate goals and use distinct checkpoint names. Your keyboard, controller, and autofire continue working alongside AI input. Human directions override opposite AI directions, and other buttons combine. Disconnecting cancels only that client's pending action and leaves other agents connected; ordinary speed is restored when the last client disconnects. An action timeout cancels that action without disconnecting other agents. Turning AI Control off releases all AI clients and AI buttons, preserving human input and the current pause state. **MCP Enabled** can prevent clients from reconnecting entirely.

**AI → AI Speed** selects 1×, 2×, 4×, 8×, or Maximum. The initial default is **4×**; selections persist. `set_speed` accepts `multiplier: 1`, `2`, `4`, `8`, or `"maximum"` and changes the same setting. Existing fast-forward menu controls and shortcuts temporarily take priority. Actual speed depends on hardware. Faster emulation reduces action time; it does not change Codex's model or inference latency.

Each `act` holds AI buttons for 1–600 emulated frames, releases them, and returns a screenshot by default. Empty buttons mean wait; `screenshot: false` returns metadata without encoding an image. `act_sequence` accepts `actions: [{ buttons: ["Right"], frames: 30 }, { buttons: ["A"], frames: 2 }]`, with 1–200 steps and at most 600 frames in total. Steps transition directly on the emulation thread and return one final screenshot by default, or metadata only with `screenshot: false`. Results include `action_start_frame`, `action_end_frame`, and per-step boundaries in `steps`; the screenshot frame may be later than completion.

For unsupported ROMs, raw actions containing a single-direction hold of at least 64 frames receive a conservative visual guard. The adapter captures screenshots in chunks of at most 32 frames and stops the remaining hold and sequence if screenshots stay stable for 64 directional frames (at most 1.25% changed pixels). `progress.reason: "visual_stall"`, `possible_obstacle: true`, and `requires_replan: true` mean inspect the screenshot and choose another route. Only fully completed sequence steps are returned. Actual action boundaries reflect the early stop. Checks capture images internally even with `screenshot:false`. Visual stability cannot prove a wall; small movements at camera boundaries may also trigger a stop, and animation can conceal blockage; screen changes never prove successful movement. Short taps, combined buttons, and verified ROM actions retain their original behavior. Restart the MCP server to load adapter changes; restarting the emulator is unnecessary.

Use batches for predictable moves and fresh screenshots near hazards. Manual pause cancels remaining steps and returns `cancelled: true`, a reason, and only fully completed step boundaries. Disabling MCP or changing games also cancels pending moves. A cancellation does not return a successful overall end frame. Observations include `human_keys`, `ai_keys`, and `effective_keys`; `keys` remains the effective emulator mask. Session metadata includes `mcp_enabled`, `ai_speed`, `frame`, and `paused`.

Games continue while Codex thinks and when the app loses focus or is minimized during AI control. The normal Pause control works; resume manually before issuing another action. Reconnect after changing, resetting, or closing a game. No ROMs are downloaded or supplied by this integration.

## Memory and checkpoints

Call `memory_map` before `read_memory`. Readable regions expose numeric byte addresses, exclusive end bounds, and bank limits. `read_memory` returns hexadecimal bytes, at most 4096 per call. Omit `segment` or use `-1` for the current mapping. Reads cannot cross regions. MMIO, BIOS, and virtual regions are excluded; memory writes are unavailable. Do not treat arbitrary bytes as health or coordinates without verifying the exact game and version.

Checkpoint names use 1–64 letters, digits, underscores, or hyphens. `save_checkpoint` overwrites that named AI checkpoint; `load_checkpoint` restores it, including saved data. Files live under `%LOCALAPPDATA%/mgba-willow/ai/checkpoints/<ROM checksum>/`, outside normal save-state slots. Normal game saves may still be written as the game runs.

## Troubleshooting and tests

- No sessions: open this checkout's app with the launcher, load a ROM, then call `list_sessions` again. Other mGBA installations do not include the bridge.
- Controlled by another client: this is an older app build. Close and reopen through the launcher to build multi-agent support. Save your progress before closing.
- Action already in progress: another agent is acting. Observe the game and retry after its action completes; do not assume your rejected move ran.
- Lost connection: reconnect; stale discovery files from crashed apps are ignored.
- Tools unavailable: run `codex mcp list`, rerun setup if needed, and restart the chat. Registration does not inject tools into an already running turn.
- Run `npm test` in `tools/mcp` for protocol tests. The C++ `platform-qt-aigameplay` test (with `BUILD_SUITE=ON`) checks directional priority and frame sequences. With a test ROM open, run `npm run smoke` for live bridge checks; this advances the game and creates an AI test checkpoint. Set `SMOKE_SESSION_ID` to an isolated disposable test session and run `npm run smoke:multi-agent` for concurrent MCP clients, busy rejection, live reads, independent disconnects, action-owner cancellation, and last-client cleanup. These live tests advance game state; never run them against your real save.
- Configure with `BUILD_AI_TESTS=ON` to build standalone `test-qt-aigameplay` and `test-qt-emeraldgamestate` without the optional cmocka dependency. Run both via `ctest --test-dir <build>/qt -R "platform-qt-(aigameplay|emeraldgamestate)" --output-on-failure`. The adapter fixtures cover dynamic pointers, collision routing, encounters/dialogue/human interruptions, stalled movement, transitions, readiness/timeouts and encrypted data.

The bridge uses a Windows named pipe restricted to the current user. There is no network listener or public service.

