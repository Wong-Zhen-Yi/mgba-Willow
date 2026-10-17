import asyncio
import json
from pathlib import Path
import unittest

from mgba_willow.bridge import Bridge, WillowError, descriptors, list_sessions
from helpers import FakeEmulator


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def test_discovery_ignores_stale_and_malformed_descriptors(self):
        async with FakeEmulator() as emulator:
            (emulator.directory / ("0" * 36 + ".json")).write_text("{}")
            (emulator.directory / ("1" * 36 + ".json")).write_text("invalid")
            stale = {"session_id": "2" * 36, "protocol": 1, "pipe": "mgba-willow-ai-" + "2" * 36}
            (emulator.directory / ("2" * 36 + ".json")).write_text(json.dumps(stale))
            (emulator.directory / "ignored.json").write_text("{}")
            sessions = await list_sessions(emulator.directory)
            self.assertEqual([session["session_id"] for session in sessions], [emulator.id])
            self.assertEqual(descriptors(Path(emulator.directory) / "missing"), [])

    async def test_fragmented_utf8_and_unknown_id(self):
        async with FakeEmulator() as emulator:
            async def handler(request, writer):
                message = (json.dumps({"id": request["id"], "result": {"text": "你好"}}, ensure_ascii=False) + "\n").encode()
                writer.write(b'{"id":999,"result":{}}\n' + message[:-3])
                await writer.drain()
                await asyncio.sleep(0)
                writer.write(message[-3:])
                return None
            emulator.handler = handler
            bridge = await Bridge.open(emulator.descriptor)
            try:
                self.assertEqual(await bridge.request("observe"), {"text": "你好"})
            finally:
                await bridge.aclose()

    async def test_bridge_error_does_not_poison_connection(self):
        async with FakeEmulator() as emulator:
            async def handler(request, writer):
                if request["method"] == "fail":
                    writer.write((json.dumps({"id": request["id"], "error": "Rejected"}) + "\n").encode())
                    return None
                return {"ok": True}
            emulator.handler = handler
            bridge = await Bridge.open(emulator.descriptor)
            try:
                with self.assertRaisesRegex(WillowError, "Rejected"):
                    await bridge.request("fail")
                self.assertEqual(await bridge.request("session"), {"ok": True})
            finally:
                await bridge.aclose()

    async def test_timeout_closes_connection_without_retry(self):
        async with FakeEmulator() as emulator:
            async def handler(request, writer):
                return None
            emulator.handler = handler
            bridge = await Bridge.open(emulator.descriptor, request_timeout=0.03)
            with self.assertRaisesRegex(WillowError, "timed out"):
                await bridge.request("act", {"buttons": ["A"], "frames": 1})
            await bridge.aclose()
            self.assertEqual(len(emulator.calls), 1)
            self.assertTrue(bridge.writer.is_closing())

    async def test_cancellation_closes_pipe(self):
        async with FakeEmulator() as emulator:
            received = asyncio.Event()
            async def handler(request, writer):
                received.set()
                return None
            emulator.handler = handler
            bridge = await Bridge.open(emulator.descriptor)
            task = asyncio.create_task(bridge.request("act"))
            await received.wait()
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.assertTrue(bridge.writer.is_closing())
            await bridge.aclose()
            await asyncio.wait_for(emulator.disconnected.wait(), 1)

    async def test_malformed_oversized_and_eof_close_pipe(self):
        for data in (b"invalid-json\n", b'[]\n', b'{"id":1,"result":[]}\n', b"x" * (16 * 1024 * 1024 + 1), b""):
            with self.subTest(size=len(data)):
                async with FakeEmulator() as emulator:
                    async def handler(request, writer):
                        writer.write(data)
                        if not data:
                            writer.close()
                        return None
                    emulator.handler = handler
                    bridge = await Bridge.open(emulator.descriptor)
                    with self.assertRaises(WillowError):
                        await bridge.request("observe")
                    self.assertTrue(bridge.writer.is_closing())
                    await bridge.aclose()

    async def test_descriptor_validation(self):
        for descriptor in ({}, {"protocol": True, "pipe": "mgba-willow-ai-" + "0" * 36},
                           {"protocol": 1, "pipe": "../../elsewhere"}, {"protocol": 2, "pipe": "mgba-willow-ai-" + "0" * 36}):
            with self.assertRaisesRegex(WillowError, "descriptor"):
                await Bridge.open(descriptor)

    async def test_session_probe_honors_request_timeout(self):
        async with FakeEmulator() as emulator:
            async def handler(request, writer):
                return None
            emulator.handler = handler
            self.assertEqual(await list_sessions(emulator.directory, request_timeout=0.03), [])
