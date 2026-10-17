"""Send identical fixtures through Node and Python; compare outputs and IPC calls."""

import base64
from copy import deepcopy
import json
from pathlib import Path
import shutil
import struct
import subprocess
import unittest
import zlib

from mgba_willow.policy import input_problem, guarded_verified_input, train_battle
from mgba_willow.state import StateResponses, compact_state, handoff
from mgba_willow.visual import guarded_movement, pixels
from helpers import state, battle


def png(filter_kind=0, color=0):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    header = struct.pack(">IIBBBBB", 16, 16, 8, 2, 0, 0, 0)
    # All five row filters decode this zero-filled image to black.
    raw = bytes([filter_kind] + [color] * 48) * 16
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    return base64.b64encode(data).decode()


def fixtures():
    cases = []
    initial = state(party={"available": True, "members": [{"hp": 20, "moves": [{"id": 1}], "slot": 0}]}, inventory={"items": [1]})
    next_state = {**initial, "frame": 11, "party": {"members": [{"hp": 10}]}}
    del next_state["inventory"]
    for options in ({}, {"compact": True}, {"full": True}):
        cases.append({"kind": "state", "responses": [{"game_state": initial}, {"game_state": next_state},
                                                     {"game_state": battle()}, {"game_state": state(frame=1)},
                                                     {"game_state": state(frame=20, map={"id": 2})}], **options})
    cases += [{"kind": "compact", "state": initial}, {"kind": "compact", "state": state(party=[])},
              {"kind": "handoff", "state": initial}, {"kind": "handoff", "state": {"supported": False}},
              {"kind": "handoff", "state": state(recent_events=[{"type": "save_confirmed", "frame": 1}])}]
    for current, method, args in [
        (state(), "press", {"button": "A"}),
        (state(), "press", {"button": "A", "hold_frames": 60}),
        (state(), "press", {"button": "A", "expected_menu": {"kind": "battle_move"}}),
        ({"supported": False}, "press", {"button": "A", "expected_menu": {"kind": "start"}}),
        (state(movement_available=False), "press", {"button": "Right"}),
        (battle(), "act", {"buttons": ["Down"], "frames": 60}),
        (state(), "act_sequence", {"actions": [{"buttons": ["A"], "frames": 1}] * 2}),
        ({"supported": False}, "act", {"buttons": ["A"], "frames": 60}),
        (state(), "act", {"buttons": [], "frames": 60}),
    ]:
        cases.append({"kind": "input", "state": current, "method": method, "args": args})
    for change in ({"interaction": "battle"}, {"map": {"id": 2}}, {"recent_events": [{"text": "Switch?"}]},
                   {"movement_available": False}, {}):
        cases.append({"kind": "verified", "method": "act_sequence", "before": {"game_state": state()},
                      "args": {"actions": [{"buttons": ["Right"], "frames": 2}], "screenshot": False},
                      "responses": [{"game_state": state(), "action_start_frame": 10, "action_end_frame": 11},
                                    {"game_state": state(**change), "action_start_frame": 11, "action_end_frame": 12}]})
    for extra in ({"human_keys": 1}, {"cancelled": True}):
        cases.append({"kind": "verified", "method": "act", "before": {"game_state": state()},
                      "args": {"buttons": ["Right"], "frames": 10, "screenshot": False},
                      "responses": [{"game_state": state(), "action_start_frame": 10, "action_end_frame": 11, **extra}]})
    cases.append({"kind": "verified", "method": "act", "before": {"game_state": state()},
                  "args": {"buttons": ["Right"], "frames": 100, "screenshot": False},
                  "responses": [{"game_state": state(), "action_start_frame": 10 + i, "action_end_frame": 11 + i} for i in range(60)]})
    cases.append({"kind": "verified", "method": "act", "before": {"game_state": state()},
                  "args": {"buttons": [], "frames": 8},
                  "responses": [{"game_state": state(), "action_start_frame": 10, "action_end_frame": 18},
                                {"game_state": state(frame=19), "png": png()}]})
    current = battle()
    faint = deepcopy(current)
    faint["battle"]["battlers"][1]["hp"] = 0
    cases.append({"kind": "training", "args": {"move_slot": 0},
                  "responses": [{"game_state": current}, {"game_state": current}, {"game_state": faint}]})
    cases.append({"kind": "training", "args": {"move_slot": 0}, "responses": [{"game_state": current}] * 3})
    for menu in ({"kind": "party"}, {**current["menu"], "selected_move": {"id": 1, "pp": 0}}):
        cases.append({"kind": "training", "args": {"move_slot": 0}, "responses": [{"game_state": {**current, "menu": menu}}]})
    for flags in (1, 2, 5, 6, 8, 12, 16, 128, 260):
        cases.append({"kind": "training", "args": {"move_slot": 0},
                      "responses": [{"game_state": {**current, "battle": {**current["battle"], "type_flags": flags}}}]})
    for text in ("Pokemon is trying to learn TACKLE!", "Evolution!", "Enemy fainted!"):
        cases.append({"kind": "training", "args": {"move_slot": 0},
                      "responses": [{"game_state": current}, {"game_state": {**current, "recent_events": [{"text": text}]}}]})
    for extra in ({"human_keys": 1}, {"paused": True}, {"cancelled": True}):
        cases.append({"kind": "training", "args": {"move_slot": 0}, "responses": [{"game_state": current, **extra}]})
    animation = {**current, "menu": {"kind": "none"}}
    cases.append({"kind": "training", "args": {"move_slot": 0},
                  "responses": [{"game_state": animation}, {"game_state": {**current, "menu": {"kind": "evolution"}}}]})
    # Moving a battle cursor then using A is bounded by the action budget.
    cursor_two = {**current, "menu": {**current["menu"], "cursor": 2}}
    cases.append({"kind": "training", "args": {"move_slot": 0, "max_actions": 1},
                  "responses": [{"game_state": cursor_two}, {"game_state": cursor_two}, {"game_state": current}]})
    for filter_kind in range(5):
        image = png(filter_kind)
        cases.append({"kind": "visual", "method": "act_sequence",
                      "args": {"actions": [{"buttons": ["Right"], "frames": 120}, {"buttons": ["A"], "frames": 1}], "screenshot": False},
                      "responses": [{"game_state": {"supported": False}, "png": image},
                                    {"png": image, "action_start_frame": 10, "action_end_frame": 42},
                                    {"png": image, "action_start_frame": 42, "action_end_frame": 74}]})
    cases.append({"kind": "visual", "method": "act", "args": {"buttons": ["Right"], "frames": 100},
                  "responses": [{"game_state": {"supported": False}, "png": "invalid"}, {"completed": True}]})
    return cases


async def run_python(fixture):
    calls, cursor = [], 0

    async def request(method, args):
        nonlocal cursor
        calls.append({"method": method, "args": deepcopy(args)})
        if cursor >= len(fixture["responses"]):
            raise ValueError("Fixture responses exhausted")
        response = deepcopy(fixture["responses"][cursor])
        cursor += 1
        return response

    try:
        kind = fixture["kind"]
        if kind == "state":
            states = StateResponses()
            result = [states.apply(response, fixture.get("full", False), fixture.get("compact", False)) for response in fixture["responses"]]
        elif kind == "compact":
            result = compact_state(fixture["state"])
        elif kind == "handoff":
            result = handoff(fixture["state"])
        elif kind == "input":
            result = input_problem(fixture["state"], fixture["method"], fixture["args"])
        elif kind == "verified":
            result = await guarded_verified_input(request, fixture["method"], fixture["args"], fixture["before"])
        elif kind == "visual":
            result = await guarded_movement(request, fixture["method"], fixture["args"])
        elif kind == "training":
            result = await train_battle(request, fixture["args"])
        return {"result": result, "calls": calls}
    except Exception as error:
        return {"error": str(error), "calls": calls}


class ParityTests(unittest.IsolatedAsyncioTestCase):
    @unittest.skipUnless(shutil.which("node"), "Node is only needed for development parity comparisons")
    async def test_node_python_behavior_parity(self):
        cases = fixtures()
        process = subprocess.run(["node", str(Path(__file__).with_name("oracle.mjs"))],
                                 input=json.dumps(cases), capture_output=True, text=True, check=True)
        expected = json.loads(process.stdout)
        for index, (fixture, reference) in enumerate(zip(cases, expected, strict=True)):
            with self.subTest(index=index, kind=fixture["kind"]):
                actual = await run_python(fixture)
                self.assertEqual(actual, reference)
                self.assertNotEqual(actual.get("error"), "Fixture responses exhausted")

    async def test_native_policy_fixtures_without_node(self):
        # These run even on machines without Node installed.
        for index, fixture in enumerate(fixtures()):
            with self.subTest(index=index, kind=fixture["kind"]):
                actual = await run_python(fixture)
                self.assertNotIn("error", actual)
        self.assertIsNone(pixels("invalid"))
        for filter_kind in range(5):
            self.assertEqual(pixels(png(filter_kind))[3], bytes(16 * 16 * 3))
