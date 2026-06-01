"""
SC Terminal — PC Bridge
=======================
Tails the Star Citizen Game.log file, parses relevant events,
and pushes them as JSON frames to all connected terminal WebSockets.

Usage:
    python bridge.py [--log PATH] [--port PORT] [--host HOST]

Defaults:
    --log   C:\\Roberts Space Industries\\StarCitizen\\LIVE\\Game.log
    --port  8765
    --host  0.0.0.0   (listen on all interfaces)

Install deps:
    pip install -r requirements.txt

Security notes:
  - This process only READS Game.log (a plain text file).
  - No game memory is accessed.
  - No OS input injection is performed.
  - EasyAntiCheat is not triggered by read-only file monitoring.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import time
from pathlib import Path
from typing import Any

import websockets
from websockets.server import WebSocketServerProtocol

from event_parser import parse_log_line

# ── Logging ──────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("sc_bridge")

# ── Defaults ─────────────────────────────────────────────────────────────────

DEFAULT_LOG_PATH = (
    r"C:\Roberts Space Industries\StarCitizen\LIVE\Game.log"
)
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8765

# ── Connected terminals ───────────────────────────────────────────────────────

_clients: set[WebSocketServerProtocol] = set()


async def broadcast(frame: dict[str, Any]) -> None:
    """Send a JSON event frame to all connected terminals."""
    if not _clients:
        return
    data = json.dumps(frame)
    dead = set()
    for ws in _clients:
        try:
            await ws.send(data)
        except websockets.ConnectionClosed:
            dead.add(ws)
    _clients.difference_update(dead)


async def ws_handler(ws: WebSocketServerProtocol, path: str) -> None:
    """Handle a new WebSocket connection from a terminal."""
    addr = ws.remote_address
    log.info("Terminal connected: %s (path=%s)", addr, path)
    _clients.add(ws)
    try:
        # Keep the connection alive; terminals send no upstream messages.
        async for _ in ws:
            pass
    except websockets.ConnectionClosed:
        pass
    finally:
        _clients.discard(ws)
        log.info("Terminal disconnected: %s", addr)


# ── Game.log tailer ──────────────────────────────────────────────────────────

async def tail_log(log_path: Path) -> None:
    """
    Continuously tail Game.log and dispatch parsed events.
    Seeks to end of file on startup (skips historical entries),
    then follows new lines as they are written.
    """
    log.info("Watching: %s", log_path)

    while not log_path.exists():
        log.warning("Game.log not found — is Star Citizen running? Retrying in 5 s…")
        await asyncio.sleep(5)

    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        # Seek to end — don't replay old events on startup
        f.seek(0, 2)

        while True:
            line = f.readline()
            if not line:
                await asyncio.sleep(0.05)
                continue

            line = line.rstrip("\n\r")
            if not line:
                continue

            event = parse_log_line(line)
            if event:
                event["ts"] = int(time.time() * 1000)
                log.debug("Event: %s", event)
                await broadcast(event)


# ── Entry point ───────────────────────────────────────────────────────────────

async def main(log_path: Path, host: str, port: int) -> None:
    server = await websockets.serve(ws_handler, host, port)
    log.info("SC Bridge listening on ws://%s:%d/terminal", host, port)
    await asyncio.gather(
        server.wait_closed(),
        tail_log(log_path),
    )


def cli() -> None:
    parser = argparse.ArgumentParser(description="SC Terminal PC Bridge")
    parser.add_argument(
        "--log",
        default=DEFAULT_LOG_PATH,
        help="Path to Star Citizen Game.log",
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="WebSocket bind host")
    parser.add_argument("--port", default=DEFAULT_PORT, type=int, help="WebSocket port")
    args = parser.parse_args()

    asyncio.run(main(Path(args.log), args.host, args.port))


if __name__ == "__main__":
    cli()
