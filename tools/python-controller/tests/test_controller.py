import asyncio
from copy import deepcopy
import unittest

from mgba_willow import WillowController, WillowError
from mgba_willow.controller import TOOLS
from helpers import FakeEmulator, state
from test_parity import png


class ControllerTests(unittest.IsolatedAsyncioTestCase):
    async def test_public_methods_cover_every_tool(self):
        for name in TOOLS:
            self.assertTrue(callable(getattr(WillowController, name, None)), name)

    async def test_validation_occurs_before_connection_or_input(self):
        controller = WillowController()
        for name, args in [
            ("connect", {"session_id": "invalid"}), ("press", {"button": "X"}),
            ("press", {"button": "A", "hold_frames": 0}), ("act", {"buttons": ["A"], "frames": True}),
            ("act_sequence", {"actions": [{"buttons": [], "frames": 400}] * 2}),
            ("read_memory", {"address": 0, "length": 4097}), ("get_local_map", {"radius": 17}),
            ("save_checkpoint", {"name": "../escape"}), ("set_speed", {"multiplier": 3}),
            ("set_objective", {"text": "x" * 513}), ("unknown", {}),
        ]:
            with self.subTest(name=name, args=args):
                with self.assertRaises(WillowError):
                    await controller.call_tool(name, args)
        with self.assertRaisesRegex(WillowError, "No game connected"):
            await controller.observe()

    async def test_state_views_and_checkpoint_reset(self):
        async with FakeEmulator() as emulator, WillowController(emulator.directory) as controller:
            self.assertEqual((await controller.list_sessions())["sessions"][0]["session_id"], emulator.id)
            connected = await controller.connect(emulator.id)
            self.assertEqual(connected["state_format"], "compact")
            emulator.current = state(frame=11, inventory={"items": [1]})
            observed = await controller.get_game_state(full_state=True, screenshot=False)
            self.assertEqual(observed["game_state"]["inventory"], {"items": [1]})
            emulator.current = state(frame=12)
            delta = await controller.observe(compact=False, screenshot=False)
            self.assertEqual(delta["base_frame"], 11)
            self.assertIn("inventory", delta["removed_state_fields"])
            restored = await controller.load_checkpoint("synthetic")
            self.assertEqual(restored["state_format"], "compact")
            # A forced full snapshot still includes all original state.
            self.assertEqual((await controller.observe(full_state=True))["state_format"], "full")
            self.assertEqual(await controller.disconnect(), {"paused": False})
            self.assertIsNone(controller.state.previous)
            self.assertIsNone(controller.bridge)

    async def test_stale_menu_and_confirmation_batch_send_no_input(self):
        async with FakeEmulator() as emulator, WillowController(emulator.directory) as controller:
            await controller.connect(emulator.id)
            with self.assertRaisesRegex(WillowError, "Menu changed"):
                await controller.press("A", expected_menu={"kind": "battle_move"})
            with self.assertRaisesRegex(WillowError, "single-button tap"):
                await controller.act_sequence([{"buttons": ["A"], "frames": 1}] * 2)
            self.assertFalse(any(call["method"] in ("act", "press", "act_sequence") for call in emulator.calls))

    async def test_selective_screenshot_and_explicit_suppression(self):
        async with FakeEmulator() as emulator, WillowController(emulator.directory) as controller:
            await controller.connect(emulator.id)
            async def handler(request, writer):
                return {"game_state": deepcopy(emulator.current), **({"png": png()} if request["args"].get("screenshot") else {})}
            emulator.handler = handler
            result = await controller.observe()
            self.assertNotIn("png", result)
            emulator.current = state(interaction="dialogue", menu={"kind": "dialogue"})
            result = await controller.observe()
            self.assertIn("png", result)
            self.assertEqual(emulator.calls[-1]["args"], {"screenshot": True})
            result = await controller.observe(screenshot=False)
            self.assertNotIn("png", result)
            self.assertFalse(emulator.calls[-1]["args"]["screenshot"])

    async def test_handoff_preserves_historical_save_and_trims_default(self):
        async with FakeEmulator() as emulator, WillowController(emulator.directory) as controller:
            emulator.current = state(inventory={"items": [1]}, recent_events=[
                {"type": "save_confirmed", "frame": 1, "map": {"id": 8}},
                *[{"text": str(i)} for i in range(8)]])
            await controller.connect(emulator.id)
            result = await controller.get_handoff()
            self.assertEqual(result["handoff"]["last_observed_save"]["map"], {"id": 8})
            self.assertEqual(len(result["handoff"]["recent_events"]), 5)
            self.assertNotIn("inventory", result["handoff"])
            result = await controller.get_handoff(full_state=True)
            self.assertEqual(result["handoff"]["inventory"], {"items": [1]})

    async def test_tool_calls_are_serialized_and_context_closes_connection(self):
        async with FakeEmulator() as emulator:
            active, maximum = 0, 0
            async def handler(request, writer):
                nonlocal active, maximum
                active += 1
                maximum = max(maximum, active)
                await asyncio.sleep(0.01)
                active -= 1
                return {"game_state": state()}
            emulator.handler = handler
            async with WillowController(emulator.directory) as controller:
                await controller.connect(emulator.id)
                await asyncio.gather(*(controller.observe(screenshot=False) for _ in range(4)))
                self.assertEqual(maximum, 1)
            await asyncio.wait_for(emulator.disconnected.wait(), 1)

    async def test_reconnect_closes_previous_session_and_resets_baseline(self):
        async with FakeEmulator() as first, FakeEmulator() as second, WillowController(first.directory) as controller:
            await controller.connect(first.id)
            controller.session_dir = second.directory
            await controller.connect(second.id)
            self.assertEqual(first.calls[-1]["method"], "disconnect")
            self.assertEqual(controller.bridge.next_id, 2)
            self.assertEqual(controller.state.previous, second.current)

    async def test_cancelling_controller_action_releases_connection(self):
        async with FakeEmulator() as emulator, WillowController(emulator.directory) as controller:
            await controller.connect(emulator.id)
            received = asyncio.Event()
            async def handler(request, writer):
                received.set()
                return None
            emulator.handler = handler
            task = asyncio.create_task(controller.press("A"))
            await received.wait()
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.assertIsNone(controller.bridge)
            await asyncio.wait_for(emulator.disconnected.wait(), 1)
