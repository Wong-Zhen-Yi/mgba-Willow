# Local Kev-4B player (experimental)

Run [Kev-4B Artemis Q4_K_M](https://huggingface.co/turintech/Kev-4B-Artemis-GGUF)
through llama.cpp and choose bounded emulator actions without Codex, a hosted
API, or MCP registration. Optional GPT-6 Luna recovery uses a paid remote API
only when explicitly enabled. **This is not a general GBA vision agent or a proven
autonomous Pokémon playthrough.**

## One launcher, opt-in local model

1. Double-click **`Launch mGBA.cmd`**, the only double-click launcher. It opens
   the build-aware emulator and game library. It does **not** check, download or
   start Kev. Loading a game does not start Kev either.
2. Open a game, then click **Enable local model** in the bottom status bar.
   Choose a prewritten goal in the dialog and click **Start with selected goal**;
   there is no need to type instructions. The dialog previews the full goal and
   remembers your last selection, but never starts automatically. **Cancel**
   starts nothing; **Enable model only** starts the model without gameplay input.
   Confirming either enable/start option authorizes automatic setup of missing
   model/runtime files. Downloads are checksum-verified, resumable and reused.
3. NVIDIA detection selects CUDA when available, otherwise CPU. A ready Kev
   server is reused; otherwise a server console opens and startup waits up to
   three minutes. **Start with selected goal** begins that exact preset when the
   model is ready. Make an in-game save and disconnect other agents before
   confirming Start. **Enable model only** waits for a later preset selection;
   its console provides a numbered menu plus an optional custom-goal choice.
4. Click **Disable local model** to stop this helper, player, and its owned
   server. A reused external server is never stopped. Closing/changing the
   game or closing mGBA also stops the helper. Every new game/app session starts
   with the model off; this button is not a persisted auto-start preference.

Setup errors never close your game. Click Enable local model again to retry.
Turning off **AI → MCP Enabled** also stops the helper. **AI → AI Control**
stops gameplay control but does not shut down an otherwise enabled model.
Disconnecting does not pause or save the game. Supervise the first run.

### Preset gameplay goals

- **Progress the story**: work toward the next story objective from the current
  location, stopping for unknown dialogue/choices and safe in-game-save points.
- **Level up Pokémon**: train the current party through nearby wild/trainer
  battles; check HP/status/PP and stop for healing, learning or evolution choices.
- **Explore nearby**: use known live walkable tiles, inspect each move and stop
  instead of repeating blocked movement.
- **Finish the current battle**: use verified moves/targets and stop after the
  battle or when human help is needed.

The shared catalog is [`goals.json`](goals.json). Each instruction fits the
bridge's 512-character objective limit and retains the existing safety stops.
These presets express intent, not a guarantee of autonomous story progression,
level gains, healing or saving. The bounded Emerald adapter still stops at
unsupported prompts. After a player run stops, the console offers the same
presets by number; choose **C** only if you want to write a custom goal, or
**0 / Enter** to keep manual control.

### First-run downloads and storage

Confirming Start with selected goal or Enable model only downloads only missing/invalid files:

- **2.54 GB** Kev Artemis Q4_K_M GGUF.
- About **583 MB** of CUDA runtime ZIPs, or **24 MB** for CPU.
- About **38 MB** for a private portable Node.js runtime if Node.js 20+ is not
  already installed. No global PATH changes, npm install, Python, uv, original
  Qwen base model, or ROM download is required.

All assets have pinned HTTPS URLs, byte sizes and SHA-256 hashes in
[`runtime.json`](runtime.json). CUDA/CPU reuse the same model. Files live outside
this repository:

| Content | Location |
| --- | --- |
| Model | `%LOCALAPPDATA%\mgba-willow\models\Kev-4B-Artemis-Q4_K_M.gguf` |
| CUDA runtime | `%LOCALAPPDATA%\mgba-willow-tools\llama-b11804-cuda\` |
| CPU runtime | `%LOCALAPPDATA%\mgba-willow-tools\llama-b11804-cpu\` |
| Private Node runtime, if needed | `%LOCALAPPDATA%\mgba-willow-tools\node-v24.22.1\` |
| llama.cpp ZIPs | `%LOCALAPPDATA%\mgba-willow-tools\downloads\` |
| Interrupted downloads | Beside their destination, with `.partial` appended |

This is an explicit GGUF, not Ollama's store or the Hugging Face cache. Old
Python-server/Hugging Face files are neither used nor deleted. Close the model
before moving/deleting its files. Deleting the GGUF makes the next button click
install it again, not the next emulator launch.

To move PCs, copy this checkout including `tools/local-model/` and the single
launcher. Enable the model in the new app to install per-PC dependencies, or
copy the GGUF to the new user's folder; its checksum is verified before reuse.
Save files must be migrated separately.

## Important game/model limits

Kev is a **text-only choice scorer**, not a chat or screenshot model. We use
llama.cpp's `POST /v1/systemone`, provide named action options and read
`answers.action.probabilities`, not the chance-adjusted `confidence` field.
Only the bridge's two verified English Pokémon Emerald hashes enable the
initial `emerald_en_v1` adapter; see [MCP docs](../mcp/README.md).

It can select adjacent live walkable tiles with native `move_to` revalidation,
allow trainer/wild encounters, interact once in the overworld, navigate decoded
battle cursors, select Fight/a move with PP/a verified opponent target, wait
without buttons, or stop. Unknown dialogue, Bag/Party/Start/Save menus,
learning/evolution evidence and unsupported games require human inspection.
It does not blindly dismiss prompts, reset/restore games, or perform an in-game
save. Other games need text-state adapters or separate vision/OCR support.

The controller is bounded, not a demonstrated autonomous story player. Choice
scores rank alternatives; they are not calibrated probabilities of gameplay
success. The old fixed 0.30 navigation gate still stopped real runs after zero
or one move: the logs showed winning scores of 0.20–0.29 across six choices.
The default navigation floor is now **min(0.30, 1 / candidate count)** when Kev
returns a complete validated distribution. This accepts the winning safe tile
without demanding an impossible fixed share of a split vote. Legacy scorers
without a distribution keep 0.30. **Interactions/confirmations retain 0.60**;
explicit threshold overrides remain available. No raw/unknown actions are added.

The runner also:

- Handles eight-frame animation waits locally, without asking Kev to choose
  between waiting and stopping. An unchanged battle animation can run for up to
  1,800 wait frames instead of falsely stalling after 64 frames.
- Remembers visits to up to 2,048 tiles and sends only nearby visit counts plus
  the last 12 actions to the model. Repeated routes trigger recovery to an
  already-validated unseen neighbor, or a Luna request if no such route exists.
- Retries only transient **inference** failures twice with backoff. Emulator
  inputs and bridge failures are never blindly retried or reconnected.
- Rechecks state after either model's inference. Human input, manual pause,
  multiple agents and unsupported states still stop. Switching games cannot
  redirect a pending goal into another game.

### Why did the player stop?

The console now explains the reason, and `latest-handoff.json` retains the
explanation and stop details. Every decision log records its action category,
minimum score and whether the probability gate accepted it.

- **low_probability**: the selected action fell below its action-specific gate.
  The message includes its score and required minimum. No input was sent for
  that decision. Inspect the game or choose a more specific goal.
- **model_stop**: Kev explicitly handed control back. This is not proof the
  goal was completed. With Luna enabled, the supervisor can choose one other
  approved action; otherwise control returns to you.
- **needs_human_inspection**: unknown dialogue/menus, move learning, evolution
  or another unsupported prompt need manual handling before selecting a goal
  again. The story preset does not make those prompts autonomous.
- **human_interruption / other_agent_connected**: resume manually or disconnect
  the other agent before retrying.

`--min-probability P` explicitly overrides the gate for every action, preserving
a stricter global mode (for example `0.6`). `--min-navigation-probability P`
overrides navigation only. Neither setting bypasses state verification,
unsupported-menu stops or confirmation-repeat protection. Luna advice is a
separate, logged decision, not a fabricated Kev probability.

## Optional GPT-6 Luna help when stuck

In PowerShell, configure credentials **before starting mGBA**:

```powershell
$env:LUNA_API_KEY = '<your provider API key>'
$env:LUNA_MODEL = '<exact GPT-6 Luna API model ID from your provider>'
# Optional, for a provider other than the default OpenAI API:
# $env:LUNA_BASE_URL = 'https://your-provider.example/v1'
& '.\Launch mGBA.cmd'
```

In **Enable local model**, tick **Ask GPT-6 Luna when stuck**. The checkbox is
disabled until the key/model are configured, and is off by default. No API keys
are stored in the repository, logs or Qt settings. For the console-only player,
use `--luna-help` or set `LUNA_HELP=1`. The latter also preselects the checkbox.

This requires API access to that model and an OpenAI-compatible **chat completions**
endpoint with JSON-object responses. A Codex/ChatGPT subscription or a model
picker label does not automatically supply API access. No model identifier is
guessed: use the exact ID your provider offers. If GPT-6 Luna is not available
from that API, the integration cannot make it available; supply a supported ID
or leave assistance disabled.

Luna is asked on a model stop, rejected score, persistent model failure, stall,
route loop or unsupported prompt. It receives the goal, decoded state, local
map, bounded history and approved action choices — **no screenshot, ROM, API key
or shell access**. State can include party, inventory, position and recent game
text, and is sent to the configured remote provider. Calls may incur charges.

Each reply chooses **one supplied action**, then local inference resumes with
its short plan. Unknown prompts offer only `stop`, allowing advice but never
blind dismissal. An unverified confirmation cannot be repeated by either model.
State changes, human input or cancellation invalidate pending advice. API errors,
invalid replies and redirects never result in input. The default cap is **20
requests per run**, with no second request for an identical stuck observation;
`--max-assists 0..100` changes it. This is a call cap, not a monetary budget.
The log and handoff retain the guidance, reason and number of calls.

## Hardware and server options

Default settings use CUDA 13.4, **20 GPU layers** with remaining CPU offload,
**8,192-token context**, one request slot, `-ub 4096 -b 4096`, and no host prompt
cache. The publisher measured 7.6 GiB peak VRAM at 16K on an RTX 5090, which does
not fit comfortably alongside Windows on an 8 GB GPU. CPU offload also needs RAM.

Earlier inference-only smoke tests on this PC's RTX 5060 Ti loaded the model:
a short typed choice took 183 ms; a 5,041-token synthetic gameplay-state request
took 2.71 s. Total GPU usage afterward was 7,038 MiB, leaving 855 MiB. These are
individual requests, not peak-memory or gameplay-quality guarantees. No real
game was connected or advanced by those tests.

To force CPU for the in-app button:

```powershell
& '.\Launch mGBA.cmd' -Device cpu
```

Advanced users can still call the internal server script directly; there are
no separate double-click launchers:

```powershell
& '.\tools\local-model\serve.ps1' -ShowPaths
& '.\tools\local-model\serve.ps1' -CheckInstalled
& '.\tools\local-model\serve.ps1' -DownloadOnly
& '.\tools\local-model\serve.ps1' -GpuLayers 12
& '.\tools\local-model\serve.ps1' -ContextSize 4096
& '.\tools\local-model\serve.ps1' -Device cpu
```

A ready externally started server is reused by the button. `-Port` changes
8008; set `KEV_BASE_URL` to the same loopback HTTP origin. `KEV_API_KEY` is
optional bearer authentication and maps to `LLAMA_API_KEY` on the server.
Keep the server local. The client rejects non-loopback URLs and redirects.
Source/model provenance: GGUF revision
`4dc7e7e7409bc98dd89aa73ab83540839f0077ea`, SHA-256
`b8a3a2e8605b0b9f28a7cfb15d7b4a003ad55306c6bca3c055a62bad18b926fd`,
llama.cpp **b11804**, separately verified CUDA dependencies.

## Command-line player and evidence

With a ready server, loaded game, and Node.js 20+:

```powershell
node tools/local-model/cli.mjs --list
node tools/local-model/cli.mjs --goal "Explore nearby tiles" --steps 50 --minutes 5
node tools/local-model/cli.mjs --session <UUID> --goal "Finish the current battle"
```

Or use `npm run play:local -- --goal "Finish the current battle"` in `tools/mcp`.
`--help` lists options. Default budgets: **10,000 inspection/decision iterations,
12 hours**, 120-second Kev inference timeout (90 seconds for Luna). Use `--steps`
(up to 100,000) and `--minutes` to change the run limits. Ctrl+C disconnects the player.
Unchanged confirmation inputs are never automatically repeated; the runner
waits up to 120 frames and stops. Stale decisions consume an iteration.

Full action/state JSONL and a final handoff live under
`%LOCALAPPDATA%\mgba-willow\ai\local-model\`; `--log-dir` overrides this.
`latest-handoff.json` retains save/milestone evidence, not inferred success.
Follow [Willow verification](../../doc/willow-control.md), record milestones
immediately and make/verify actual in-game saves. Logs/checkpoints are not saves.

## Validation

```sh
node --test tools/mcp/test/local-model*.test.mjs
cd tools/mcp
npm ci
npm test
```

Automated tests include **1,500 consecutive turns through the real Kev HTTP
client against a fake scoring server**, replaying the recorded 26.4% winning
score, plus long animations, route loops, retry/cancellation and Luna safety/API
contract tests. They use synthetic bridge states, fake servers and tiny model /
runtime fixtures. They do not load weights, download large assets or advance a
real game. Gameplay quality and peak memory still need disposable live testing.
