"""Protocol-1 local IPC. No game inputs are retried after ambiguous failures."""

import asyncio
import contextlib
import json
import os
from pathlib import Path
import re
import tempfile

MAX_RESPONSE = 16 * 1024 * 1024
UUID = r"[0-9a-f-]{36}"


class WillowError(Exception):
    """A bridge, session, validation, or gameplay-policy error."""


def session_directory():
    root = Path(os.environ.get("LOCALAPPDATA") or Path.home() / ".local" / "share")
    return root / "mgba-willow" / "ai" / "sessions"


def descriptors(directory=None):
    directory = Path(directory) if directory is not None else session_directory()
    if not directory.exists():
        return []
    result = []
    for file in sorted(directory.iterdir()):
        if not re.fullmatch(UUID + r"\.json", file.name):
            continue
        try:
            item = json.loads(file.read_text(encoding="utf-8"))
            if isinstance(item, dict) and item.get("session_id", "") + ".json" == file.name:
                result.append(item)
        except (OSError, ValueError, TypeError):
            continue
    return result


class Bridge:
    def __init__(self, reader, writer, timeout=35.0):
        self.reader, self.writer = reader, writer
        self.timeout = timeout
        self.next_id = 1
        self.lock = asyncio.Lock()

    @classmethod
    async def open(cls, descriptor, connect_timeout=1.5, request_timeout=35.0):
        pipe = descriptor.get("pipe")
        if type(descriptor.get("protocol")) is not int or descriptor["protocol"] != 1 or not isinstance(pipe, str) or not re.fullmatch("mgba-willow-ai-" + UUID, pipe):
            raise WillowError("Unsupported mGBA bridge descriptor")

        async def connect():
            if os.name == "nt":
                loop = asyncio.get_running_loop()
                if not hasattr(loop, "create_pipe_connection"):
                    raise WillowError("Windows named pipes require the asyncio Proactor event loop")
                reader = asyncio.StreamReader(limit=MAX_RESPONSE)
                protocol = asyncio.StreamReaderProtocol(reader)
                transport, _ = await loop.create_pipe_connection(lambda: protocol, "\\\\.\\pipe\\" + pipe)
                return reader, asyncio.StreamWriter(transport, protocol, reader, loop)
            return await asyncio.open_unix_connection(str(Path(tempfile.gettempdir()) / pipe), limit=MAX_RESPONSE)

        try:
            reader, writer = await asyncio.wait_for(connect(), connect_timeout)
        except (OSError, TimeoutError) as error:
            raise WillowError("mGBA is not responding. Reopen the game and list sessions again.") from error
        return cls(reader, writer, request_timeout)

    def close(self):
        self.writer.close()

    async def aclose(self):
        self.close()
        with contextlib.suppress(OSError, RuntimeError):
            await self.writer.wait_closed()

    async def request(self, method, args=None):
        async with self.lock:
            if self.writer.is_closing():
                raise WillowError("No mGBA connection. Connect again.")
            request_id = self.next_id
            self.next_id += 1
            try:
                async with asyncio.timeout(self.timeout):
                    payload = json.dumps({"id": request_id, "method": method, "args": args or {}}, ensure_ascii=False, allow_nan=False)
                    self.writer.write((payload + "\n").encode("utf-8"))
                    await self.writer.drain()
                    while True:
                        line = await self.reader.readline()
                        if not line:
                            raise WillowError("mGBA disconnected. Reopen the game and connect again.")
                        if len(line) > MAX_RESPONSE:
                            raise ValueError("Bridge response exceeds size limit")
                        response = json.loads(line)
                        if not isinstance(response, dict) or "id" not in response or not ("result" in response or "error" in response):
                            raise ValueError("Malformed bridge response")
                        if response["id"] == request_id:
                            break
            except asyncio.CancelledError:
                self.close()
                raise
            except TimeoutError as error:
                self.close()
                raise WillowError("mGBA request timed out; the connection was closed to release controls.") from error
            except (OSError, ValueError, WillowError) as error:
                self.close()
                raise WillowError(str(error)) from error
            if response.get("error"):
                raise WillowError(str(response["error"]))
            if not isinstance(response.get("result"), dict):
                self.close()
                raise WillowError("Malformed bridge result")
            return response["result"]


async def list_sessions(directory=None, *, connect_timeout=1.5, request_timeout=35.0):
    async def probe(descriptor):
        bridge = None
        try:
            bridge = await Bridge.open(descriptor, connect_timeout, request_timeout)
            return await bridge.request("session")
        except (WillowError, OSError):
            return None
        finally:
            if bridge:
                await bridge.aclose()

    return [item for item in await asyncio.gather(*(probe(item) for item in descriptors(directory))) if item is not None]
