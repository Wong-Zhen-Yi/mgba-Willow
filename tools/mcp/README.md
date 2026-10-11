# Play mGBA with Luna in Codex

This local MCP controls a visible game in this checkout's Qt mGBA app. It provides screenshots, exact frame-counted button presses, read-only memory, and separate AI checkpoints. Games keep running while the model thinks. Node.js 20 or newer is required.

## Structured Emerald state and navigation

Follow [Willow control and verification](../../doc/willow-control.md) for prompt
inspection, battle-state reconciliation, live route planning, and milestone/save
logging. Verified Emerald rejects repeated or held confirmation inputs: use one
`press`, inspect the new prompt, then choose the next input. Raw travel checks
decoded state between input frames and stops with `decision_point`,
`movement_restricted`, or `human_input` when inspection is needed. Only completed
sequence steps are returned. This adds IPC overhead; prefer `move_to` for travel.

`move_to` now defaults to `avoid_trainers:true`. Live tiles expose `trainer_risk`
and NPCs expose trainer type, range, and facing. Potential sightlines are
conservative cardinal rays that account for rotation; defeat flags and sight
occlusion are not decoded. Inspect before using `avoid_trainers:false` for an
intentional battle or a route past a known defeated trainer. New hazards stop
movement with `trainer_sightline`.

`movement_available` reports the strengthened overworld readiness check, including
single/held movement animations, tile transitions, and forced movement. Battle,
dialogue, and decoded menu responses always carry full state; ordinary overworld
responses continue to use per-client deltas. `act` and `act_sequence` also accept
`full_state:true`. Restart the MCP server to load control-policy changes and
rebuild/relaunch the emulator to load decoder and routing changes.

The read-only `emerald_en_v1` adapter supports two exact English Pokémon Emerald ROM hashes: retail `f3ae088181bf583e55daf962a92bb46f4f1d07b7` and the alternate dump `4c743011d7f9af0fbc1ef1de7bff157dde718f56`. The alternate layout was checked against a read-only live snapshot for save pointers, map/grid, player objects, overworld callback and encrypted party checksums. It hashes the loaded ROM backing store after zeroing only the three cartridge GPIO header halfwords at `0xC4–0xC9` in a private copy: mGBA stores changing clock registers there, so hashing them directly would reject even supported games after they run. Responses retain the original backing hash as `rom_sha1` and report the hash used for identification as `normalized_rom_sha1`. A matching header alone or any other patched ROM does not enable the adapter. Other ROMs report `game_state.supported: false` and retain every raw tool. No ROM is supplied or downloaded.

- `get_game_state({full_state:true})` returns map coordinates, interaction, party HP/status/moves/PP, inventory, and numeric event flags. `battle.battlers` adds current HP, side/slot, party index, confusion counters, and stat changes relative to neutral. `menu` distinguishes battle action/move/target input, bag, party, start, and save phases. Battle readiness requires a live player controller, its execution bit, a valid cursor and no palette fade; stale callback pointers are insufficient. Battler IDs are resolved through positions, so target side is never guessed from an ID or screenshot. ROM tables provide move/species names where valid; secure party data and encrypted bag quantities retain their validation.
- `get_handoff({screenshot:false})` reads a fresh complete snapshot and produces location, party, inventory, verified badge progress, retained message evidence and the last observed save confirmation for copying into story notes. It does not advance frames, save, or write files. Save evidence includes its original frame/location; absence means no confirmation retained, not that the game is unsaved. Story objectives beyond badge flags still require dialogue evidence and the existing notes.
- `recent_events` retains the latest 16 expanded messages seen by the active message-window printer and save-success callbacks, with frame and location. Message-buffer text can contain pages still being printed and is not proof that an item, healing or victory completed. Save success requires the active save flow to reach its verified success callback. History resets on identification, checkpoint restore and backward snapshots; screenshots are not retained. Control codes are bounded and unsupported/unexpanded strings are omitted.
- `get_local_map({radius:4})` returns a square of nearby live collision tiles, elevations, behavior IDs, NPC positions, ladders, one-way ledges, warp destinations and map connections. Radius is 1–16. Coordinates omit Emerald's seven-tile internal border. Out-of-map or invalid backing pointers produce unknown tiles rather than walkable tiles.
- `move_to({x:12,y:8})` plans within 16 tiles of the starting position on the current map. It rechecks each next step and releases directions when movement starts. It stops at battle, dialogue, script locks, map changes, unexpected movement, human input, new obstacles, 60 frames without progress, or `max_frames` (1–600, default 600). Inspect `progress.reason` and `progress.completed` before continuing. Cardinal ledges are directional two-tile edges with validated landing collision, elevation and NPC occupancy. Animated doors are permitted only northward, as the requested destination, with a matching warp event; a transition still stops the route and requires a fresh observation. Other special traversal remains excluded; exits are never intermediate route tiles.
- `press({button:"A"})` holds for one frame, then explicitly releases for two frames. `hold_frames` and `release_frames` each accept 1–60. This tool also works on unsupported ROMs.
- `wait_until({condition:"battle_menu_ready"})` sends no AI buttons and evaluates action-menu readiness after every frame. Use `battle_move_ready` after choosing Fight and `battle_target_ready` when target selection opens. Other conditions are `overworld_ready`, `map_transition_complete`, and `dialogue`. `stable_frames` defaults to 2 (1–60); `max_frames` defaults to 600 (1–600). Human input and manual pause interrupt the wait. Bag/party context menus are identified but do not claim input readiness.

Before confirming an attack, read `menu.selected_move` and, when present, `menu.target`; check current battler HP and confusion before choosing to stay in or switch. After a story milestone, preserve the relevant message evidence and reconcile it with flags instead of treating incidental trainer battles as progress. At a stopping point, heal, perform the in-game save, verify `save_confirmed`, obtain `get_handoff`, update the story notes with the next objective and any uncertain milestones, then disconnect.

New tools return metadata without screenshots by default; request `screenshot:true` when needed. All observations and action results include decoded state. The first response has `state_format:"full"` and `game_state`. Later ordinary overworld responses use `state_format:"delta"`, `base_frame`, `game_state_changes`, and `removed_state_fields`: replace each changed top-level field and remove the listed fields. `full_state:true` refreshes the baseline. Each MCP client has its own baseline, which resets on connect, checkpoint restore, map changes and backward frame changes. `observe` retains its screenshot default and accepts `screenshot:false`.

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
- `test-qt-emeraldgamestate <rom> <wram> <iwram>` additionally checks an offline overworld snapshot against the supported hash list, initialized map/grid, player position, callback and party checksums. Snapshot files are local QA inputs and are not bundled with the tests.

The bridge uses a Windows named pipe restricted to the current user. There is no network listener or public service.

