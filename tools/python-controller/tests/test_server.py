import asyncio
from copy import deepcopy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

from mgba_willow.controller import CONTRACT, TOOLS
from helpers import FakeEmulator
from test_parity import png


class ServerTests(unittest.IsolatedAsyncioTestCase):
    async def test_stdio_initialization_tool_contract_and_calls(self):
        async with FakeEmulator() as emulator:
            async def handler(request, writer):
                if request["method"] == "session":
                    return {"session_id": emulator.id}
                if request["method"] == "set_objective":
                    return {"objective": request["args"]["text"]}
                if request["method"] == "disconnect":
                    return {"paused": False}
                return {"game_state": deepcopy(emulator.current),
                        **({"png": png()} if request["args"].get("screenshot") else {})}
            emulator.handler = handler
            params = StdioServerParameters(command=sys.executable, args=["-m", "mgba_willow"],
                                           env={"MGBA_WILLOW_SESSION_DIR": str(emulator.directory)})
            async with stdio_client(params) as (reader, writer), ClientSession(reader, writer) as client:
                initialized = await client.initialize()
                self.assertEqual(initialized.instructions, CONTRACT["instructions"])
                listed = await client.list_tools()
                self.assertEqual({tool.name for tool in listed.tools}, set(TOOLS))
                for tool in listed.tools:
                    self.assertEqual(tool.inputSchema, TOOLS[tool.name]["inputSchema"])
                    self.assertEqual(tool.description, TOOLS[tool.name]["description"])
                    self.assertEqual(tool.annotations.model_dump(exclude_none=True), TOOLS[tool.name]["annotations"])
                sessions = await client.call_tool("list_sessions", {})
                self.assertEqual(sessions.structuredContent["sessions"][0]["session_id"], emulator.id)
                missing = await client.call_tool("observe", {})
                self.assertTrue(missing.isError)
                connected = await client.call_tool("connect", {"session_id": emulator.id})
                self.assertFalse(connected.isError)
                observed = await client.call_tool("observe", {"screenshot": True, "full_state": True})
                self.assertEqual(len(observed.content), 2)
                self.assertEqual(observed.content[1].type, "image")
                self.assertEqual(observed.content[1].data, png())
                self.assertNotIn("png", observed.structuredContent)
                self.assertEqual(json.loads(observed.content[0].text), observed.structuredContent)
                self.assertEqual(observed.structuredContent["state_format"], "full")
                objective = await client.call_tool("set_objective", {"text": "治疗队伍"})
                self.assertEqual(objective.structuredContent["objective"], "治疗队伍")
                for name, args in (("press", {"button": "X"}),
                                   ("act_sequence", {"actions": [{"buttons": [], "frames": 400}] * 2}),
                                   ("unknown", {})):
                    result = await client.call_tool(name, args)
                    self.assertTrue(result.isError)
                self.assertFalse((await client.call_tool("disconnect", {})).isError)

    async def test_eof_closes_bridge_without_explicit_disconnect(self):
        async with FakeEmulator() as emulator:
            process = await asyncio.create_subprocess_exec(sys.executable, "-m", "mgba_willow",
                stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
                env={**os.environ, "MGBA_WILLOW_SESSION_DIR": str(emulator.directory)})
            async def send(message):
                process.stdin.write((json.dumps(message) + "\n").encode())
                await process.stdin.drain()
            try:
                await send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                    "protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}}})
                response = json.loads(await asyncio.wait_for(process.stdout.readline(), 10))
                self.assertEqual(response["id"], 1)
                await send({"jsonrpc": "2.0", "method": "notifications/initialized"})
                await send({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
                    "name": "connect", "arguments": {"session_id": emulator.id}}})
                response = json.loads(await asyncio.wait_for(process.stdout.readline(), 10))
                self.assertFalse(response["result"]["isError"])
                process.stdin.close()
                await asyncio.wait_for(process.wait(), 10)
                await asyncio.wait_for(emulator.disconnected.wait(), 1)
                self.assertEqual(process.returncode, 0, (await process.stderr.read()).decode())
                self.assertEqual(await process.stdout.read(), b"")
            finally:
                if process.returncode is None:
                    process.kill()
                    await process.wait()

    @unittest.skipUnless(shutil.which("node"), "Node only needed for development contract checks")
    async def test_exported_contract_matches_node_server(self):
        root = Path(__file__).resolve().parents[1]
        if not (root.parent / "mcp" / "node_modules").exists():
            self.skipTest("Run npm ci in tools/mcp to check exported schemas")
        subprocess.run(["node", str(root / "export-contract.mjs"), "--check"], check=True, capture_output=True)
