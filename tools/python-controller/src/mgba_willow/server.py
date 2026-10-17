"""Stdio-only MCP wrapper. Stdout is owned exclusively by the protocol."""

import asyncio
from contextlib import asynccontextmanager
import json

from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server
from mcp import types

from .bridge import WillowError
from .controller import CONTRACT, TOOLS, WillowController


def create_server(controller=None):
    controller = controller or WillowController()

    @asynccontextmanager
    async def lifespan(_):
        try:
            yield controller
        finally:
            await controller.aclose()

    server = Server("mgba-willow-python", version="1.0.0", instructions=CONTRACT["instructions"], lifespan=lifespan)

    @server.list_tools()
    async def list_tools():
        return [types.Tool(**tool) for tool in TOOLS.values()]

    @server.call_tool(validate_input=False)
    async def call_tool(name, arguments):
        try:
            result = await controller.call_tool(name, arguments)
            metadata = {key: value for key, value in result.items() if key != "png"}
            content = [types.TextContent(type="text", text=json.dumps(metadata, ensure_ascii=False, allow_nan=False))]
            if result.get("png"):
                content.append(types.ImageContent(type="image", mimeType="image/png", data=result["png"]))
            return types.CallToolResult(content=content, structuredContent=metadata)
        except (WillowError, OSError) as error:
            return types.CallToolResult(content=[types.TextContent(type="text", text=str(error))], isError=True)

    return server


async def serve():
    server = create_server()
    async with stdio_server() as (reader, writer):
        await server.run(reader, writer, server.create_initialization_options())


def main():
    try:
        asyncio.run(serve())
    except KeyboardInterrupt:
        pass
