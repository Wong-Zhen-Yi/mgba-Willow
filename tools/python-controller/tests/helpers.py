import asyncio
from copy import deepcopy
import json
import os
from pathlib import Path
import tempfile
from uuid import uuid4


def state(**changes):
    return {"supported": True, "available": True, "adapter": "emerald_en_v1", "frame": 10,
            "interaction": "overworld", "movement_available": True, "map": {"id": 1},
            "menu": {"kind": "none"}, "player": {"x": 1, "y": 1, "moving": False},
            "recent_events": [], **changes}


def battle(**changes):
    return state(interaction="battle",
                 menu={"kind": "battle_move", "input_ready": True, "cursor": 0, "selected_move": {"id": 1, "pp": 5}},
                 party={"available": True, "members": [{"slot": 0, "hp": 20, "species_id": 1, "level": 5, "moves": [{"id": 1}]}]},
                 battle={"available": True, "type_flags": 4, "outcome": 0,
                         "battlers": [{"available": True, "hp": 20}, {"available": True, "hp": 10}]}, **changes)


class FakeEmulator:
    """Real named pipe on Windows, real Unix socket elsewhere; no live game."""

    async def __aenter__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="willow-python-test-")
        self.directory = Path(self.temp.name)
        self.id = str(uuid4())
        self.descriptor = {"session_id": self.id, "pipe": "mgba-willow-ai-" + self.id, "protocol": 1}
        (self.directory / (self.id + ".json")).write_text(json.dumps(self.descriptor), encoding="utf-8")
        self.calls, self.writers, self.tasks = [], set(), set()
        self.current = state()
        self.handler = None
        self.disconnected = asyncio.Event()
        self.loop = asyncio.get_running_loop()
        if os.name == "nt":
            def factory():
                return asyncio.StreamReaderProtocol(asyncio.StreamReader(), client_connected_cb=self.accept)
            self.servers = await self.loop.start_serving_pipe(factory, "\\\\.\\pipe\\" + self.descriptor["pipe"])
        else:
            self.endpoint = Path(tempfile.gettempdir()) / self.descriptor["pipe"]
            self.servers = [await asyncio.start_unix_server(self.accept, str(self.endpoint))]
        return self

    def accept(self, reader, writer):
        self.writers.add(writer)
        task = asyncio.create_task(self.handle(reader, writer))
        self.tasks.add(task)
        task.add_done_callback(self.tasks.discard)

    async def handle(self, reader, writer):
        try:
            while line := await reader.readline():
                request = json.loads(line)
                self.calls.append(deepcopy(request))
                if self.handler:
                    result = await self.handler(request, writer)
                    if result is None:
                        continue
                elif request["method"] == "session":
                    result = {"session_id": self.id, "title": "Synthetic Emerald"}
                elif request["method"] == "disconnect":
                    result = {"paused": False}
                else:
                    result = {"frame": self.current["frame"], "game_state": deepcopy(self.current)}
                writer.write((json.dumps({"id": request["id"], "result": result}) + "\n").encode())
                await writer.drain()
        except (ConnectionError, asyncio.CancelledError):
            pass
        finally:
            self.writers.discard(writer)
            writer.close()
            self.disconnected.set()

    async def __aexit__(self, *_):
        for server in self.servers:
            server.close()
        for writer in list(self.writers):
            writer.close()
        for task in list(self.tasks):
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        for server in self.servers:
            if hasattr(server, "wait_closed"):
                await server.wait_closed()
        if hasattr(self, "endpoint"):
            self.endpoint.unlink(missing_ok=True)
        self.temp.cleanup()
