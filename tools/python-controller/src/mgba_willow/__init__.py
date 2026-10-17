"""Control a running Willow emulator, independently of the MCP wrapper."""

from .controller import WillowController
from .bridge import WillowError

__all__ = ["WillowController", "WillowError"]
