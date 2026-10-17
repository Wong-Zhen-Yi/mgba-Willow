"""Read a single session without advancing frames or choosing a game implicitly."""

import argparse
import asyncio
import json

from mgba_willow import WillowController


async def main(session_id):
    async with WillowController() as controller:
        if session_id is None:
            print(json.dumps(await controller.list_sessions(), indent=2, ensure_ascii=False))
            return
        connected = await controller.connect(session_id)
        print(json.dumps(connected, indent=2, ensure_ascii=False))
        print(json.dumps(await controller.observe(full_state=True, screenshot=False), indent=2, ensure_ascii=False))
        await controller.disconnect()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session-id", help="Session UUID from the listing; omit to list sessions")
    asyncio.run(main(parser.parse_args().session_id))
