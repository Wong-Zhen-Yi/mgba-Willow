# Willow Python controller and MCP server

Control a running Willow Qt emulator directly from Python, or expose the same
19 tools as the Node MCP server over stdio. Python 3.11+ is required. Node.js,
the mGBA CFFI bindings, and an emulator rebuild are not required for this controller.
The running emulator must be a Willow build with **AI → MCP Enabled**.

## Install and run on Windows

From the repository root in PowerShell:

```powershell
.\tools\python-controller\setup.ps1
.\tools\python-controller\.venv\Scripts\python.exe -m mgba_willow
```

The second command starts the stdio MCP server and waits for an MCP client.
It is not an interactive terminal UI. `mgba-willow-mcp` in the virtual environment
is an equivalent entry point.

To install and register with Codex explicitly:

```powershell
.\tools\python-controller\setup.ps1 -Register
```

This adds `mgba-willow-python` and leaves the existing Node registration intact.
Restart the chat to discover the new tools. Choose one server for a gameplay
workflow to avoid accidentally issuing duplicate commands to the shared game.
The setup script changes registration only when `-Register` is supplied.
Use `-PythonExecutable 'C:\path\to\python.exe'` to choose an interpreter.

For another stdio MCP host, use the absolute virtual-environment Python executable
as `command` and `["-m", "mgba_willow"]` as `args`. No working directory is needed.

## Install on Linux or macOS

```sh
python3 -m venv tools/python-controller/.venv
tools/python-controller/.venv/bin/python -m pip install -e tools/python-controller
tools/python-controller/.venv/bin/python -m mgba_willow
```

The controller uses Windows named pipes or Unix-domain sockets, matching the
existing bridge. Windows requires asyncio's default Proactor event loop.
Windows pipe operation is tested locally; Unix transport needs validation on a
compatible host before claiming platform support for a particular emulator build.

## Use from Python

```python
import asyncio
from mgba_willow import WillowController

async def main():
    async with WillowController() as game:
        sessions = await game.list_sessions()
        print(sessions)  # Choose the intended session UUID explicitly.
        # await game.connect("session-uuid-from-listing")
        # result = await game.observe(full_state=True, screenshot=False)
        # print(result)
        # await game.press("A", expected_menu={"kind": "battle_move", "cursor": 0})
        # Inspect each result before issuing the next input.
        # await game.disconnect()

asyncio.run(main())
```

The runnable `examples/observe.py` lists sessions by default. Pass
`--session-id UUID` to connect and read state without advancing frames.
It does not select a game automatically.

Each existing MCP tool has a named async controller method:

- Sessions: `list_sessions()`, `connect(session_id)`, `disconnect()`.
- Observation: `observe(**options)`, `get_game_state(**options)`,
  `get_handoff(**options)`, `get_local_map(**options)`.
- Controls: `set_objective(text)`, `press(button, **options)`,
  `act(buttons, frames, **options)`, `act_sequence(actions, **options)`,
  `move_to(x, y, **options)`, `wait_until(condition, **options)`,
  `train_battle(move_slot, **options)`, `set_speed(multiplier)`.
- Memory and checkpoints: `memory_map()`, `read_memory(address, length, **options)`,
  `save_checkpoint(name)`, `load_checkpoint(name)`.

`call_tool(name, arguments)` provides the same validated API dynamically.
Exceptions use `WillowError`; MCP reports them as tool errors. Controllers keep
independent connections and state baselines. Calls on one controller serialize.
Context exit closes that client's socket and releases its AI controls, including
on exceptions. An in-flight cancellation or timeout closes the connection;
reconnect and inspect before sending another action. Input is never retried
automatically. Other agents and human input remain under the emulator's control.

Methods return dictionaries. `png`, when present, is base64 PNG; MCP sends it as
an image content block and excludes it from text and structured metadata.
Default compact views replace the previous view. `full_state=True` requests a
complete snapshot; `compact=False` enables legacy deltas, which replace changed
top-level fields and delete `removed_state_fields`. Match `base_frame` before
applying a delta. State and screenshots align only when `atomic` is true.

Follow [Willow control and verification](../../doc/willow-control.md): inspect
after each decision tap, plan from live tiles, and verify in-game saves separately
from checkpoints. `move_to` defaults to avoiding trainer sightlines, as in Node;
this repository's playthrough instructions call for `avoid_trainers=False` during
ordinary travel after inspecting the route.

Sessions default to `%LOCALAPPDATA%/mgba-willow/ai/sessions`, or
`~/.local/share/mgba-willow/ai/sessions` when `LOCALAPPDATA` is unset. Set
`MGBA_WILLOW_SESSION_DIR` or pass `session_dir` to `WillowController` for an
alternative descriptor directory. This changes discovery only; descriptors must
still name valid local protocol-1 Willow endpoints. Default connection timeout is
1.5 seconds and request timeout is 35 seconds; Python callers may override them
with `connect_timeout` and `request_timeout`.

## Tests and contract maintenance

```powershell
.\tools\python-controller\.venv\Scripts\python.exe -m unittest discover -s tools/python-controller/tests -v
```

Tests use synthetic sessions and real local IPC, never a running game. They cover
MCP initialization, tool schemas, images, state views, guards, timeouts,
cancellation, and shutdown. Node comparisons run when Node is available; the
contract check also requires `npm ci` in `tools/mcp`. Other Python tests run
without Node. Live gameplay smoke tests must use disposable sessions.

The checked-in `contract.json` contains schemas, descriptions, annotations, and
server instructions exported from the Node implementation. It is package data,
so Python never invokes Node at runtime. After changing the Node tool interface:

```powershell
node tools/python-controller/export-contract.mjs
node tools/python-controller/export-contract.mjs --check
```

Port any accompanying behavior changes and rerun both test suites. The pinned
official MCP Python SDK version is declared in `pyproject.toml`.
