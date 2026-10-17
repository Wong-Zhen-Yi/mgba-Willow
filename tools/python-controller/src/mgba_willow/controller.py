"""Public Python API. Every method uses the same validation and policy as MCP."""

import asyncio
from copy import deepcopy
from importlib.resources import files
import json
import os

from jsonschema import Draft7Validator, FormatChecker

from .bridge import Bridge, WillowError, descriptors, list_sessions
from .policy import guarded_verified_input, train_battle
from .state import StateResponses, compact_state, handoff, needs_image
from .visual import guarded_movement

CONTRACT = json.loads(files(__package__).joinpath("contract.json").read_text(encoding="utf-8"))
TOOLS = {tool["name"]: tool for tool in CONTRACT["tools"]}
VALIDATORS = {name: Draft7Validator(tool["inputSchema"], format_checker=FormatChecker()) for name, tool in TOOLS.items()}


class WillowController:
    """One independent bridge connection and full-state baseline.

    Methods return metadata dictionaries, with base64 PNG in ``png`` when requested.
    Results default to compact state. Use ``full_state=True`` for full snapshots or
    ``compact=False`` for legacy deltas. Consume every response in order.
    """

    def __init__(self, session_dir=None, *, connect_timeout=1.5, request_timeout=35.0):
        self.session_dir = session_dir or os.environ.get("MGBA_WILLOW_SESSION_DIR")
        self.connect_timeout = connect_timeout
        self.request_timeout = request_timeout
        self.bridge = None
        self.state = StateResponses()
        self.lock = asyncio.Lock()

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_):
        await self.aclose()

    async def aclose(self):
        # Closing the socket immediately cancels this client's action in mGBA.
        bridge, self.bridge = self.bridge, None
        self.state.reset()
        if bridge:
            await bridge.aclose()

    async def _request(self, method, args):
        if self.bridge is None:
            raise WillowError("No game connected. Use list_sessions, then connect.")
        result = await self.bridge.request(method, args)
        if result.get("error"):
            raise WillowError(str(result["error"]))
        return result

    async def call_tool(self, name, arguments=None):
        """Call any named Willow tool; identical semantics for scripts and MCP."""
        if name not in TOOLS:
            raise WillowError(f"Unknown tool: {name}")
        args = deepcopy(arguments if arguments is not None else {})
        errors = list(VALIDATORS[name].iter_errors(args))
        if errors:
            raise WillowError(f"Invalid {name} arguments: {errors[0].message}")
        if name == "act_sequence" and sum(action["frames"] for action in args["actions"]) > 600:
            raise WillowError("Sequence exceeds 600 total frames")
        # JSON Schema integer also accepts 1.0; normalize before bit operations.
        if name == "train_battle":
            args["move_slot"] = int(args["move_slot"])
        async with self.lock:
            try:
                selective = ("screenshot" in TOOLS[name]["inputSchema"].get("properties", {})
                             and "screenshot" not in args and (self.state.previous or {}).get("supported") is True)
                result = await self._dispatch(name, {**args, "screenshot": False} if selective else args)
                if selective and needs_image(result.get("game_state")):
                    result = {**result, **await self._request("observe", {"screenshot": True})}
                return self.state.apply(result, args.get("full_state") is True, args.get("compact") is not False)
            except asyncio.CancelledError:
                await self.aclose()
                raise

    async def _dispatch(self, name, args):
        if name == "list_sessions":
            return {"sessions": await list_sessions(self.session_dir, connect_timeout=self.connect_timeout,
                                                   request_timeout=self.request_timeout)}
        if name == "connect":
            self.state.reset()
            if self.bridge:
                try:
                    await self._request("disconnect", {})
                except WillowError:
                    pass
                await self.aclose()
            descriptor = next((item for item in descriptors(self.session_dir) if item["session_id"] == args["session_id"]), None)
            if descriptor is None:
                raise WillowError("Session not found. Use list_sessions again.")
            candidate = await Bridge.open(descriptor, self.connect_timeout, self.request_timeout)
            try:
                result = await candidate.request("connect")
                self.bridge = candidate
                return result
            except BaseException:
                await candidate.aclose()
                raise
        if name == "disconnect":
            try:
                return await self._request(name, args)
            finally:
                await self.aclose()
        if name == "load_checkpoint":
            self.state.reset()
        if name == "get_handoff":
            result = await self._request("get_game_state", args)
            summary = handoff(result.get("game_state"))
            if args.get("full_state") is not True and args.get("compact") is not False:
                summary.pop("inventory", None)
                summary["party"] = compact_state(result.get("game_state") or {}).get("party")
                if "recent_events" in summary:
                    summary["recent_events"] = summary["recent_events"][-5:]
            return {**result, "handoff": summary}
        if name == "train_battle":
            return await train_battle(self._request, args)
        if name in ("press", "act", "act_sequence"):
            if (self.state.previous or {}).get("supported") is True or args.get("expected_menu"):
                before = await self._request("get_game_state", {"screenshot": False})
                return await guarded_verified_input(self._request, name, args, before)
            if name != "press":
                return await guarded_movement(self._request, name, args)
        return await self._request(name, args)

    async def list_sessions(self):
        return await self.call_tool("list_sessions")

    async def connect(self, session_id):
        return await self.call_tool("connect", {"session_id": session_id})

    async def set_objective(self, text):
        return await self.call_tool("set_objective", {"text": text})

    async def observe(self, **options):
        return await self.call_tool("observe", options)

    async def get_game_state(self, **options):
        return await self.call_tool("get_game_state", options)

    async def get_handoff(self, **options):
        return await self.call_tool("get_handoff", options)

    async def get_local_map(self, **options):
        return await self.call_tool("get_local_map", options)

    async def move_to(self, x, y, **options):
        return await self.call_tool("move_to", {"x": x, "y": y, **options})

    async def wait_until(self, condition, **options):
        return await self.call_tool("wait_until", {"condition": condition, **options})

    async def press(self, button, **options):
        return await self.call_tool("press", {"button": button, **options})

    async def train_battle(self, move_slot, **options):
        return await self.call_tool("train_battle", {"move_slot": move_slot, **options})

    async def act(self, buttons, frames, **options):
        return await self.call_tool("act", {"buttons": buttons, "frames": frames, **options})

    async def act_sequence(self, actions, **options):
        return await self.call_tool("act_sequence", {"actions": actions, **options})

    async def set_speed(self, multiplier):
        return await self.call_tool("set_speed", {"multiplier": multiplier})

    async def memory_map(self):
        return await self.call_tool("memory_map")

    async def read_memory(self, address, length, **options):
        return await self.call_tool("read_memory", {"address": address, "length": length, **options})

    async def save_checkpoint(self, name):
        return await self.call_tool("save_checkpoint", {"name": name})

    async def load_checkpoint(self, name):
        return await self.call_tool("load_checkpoint", {"name": name})

    async def disconnect(self):
        return await self.call_tool("disconnect")
