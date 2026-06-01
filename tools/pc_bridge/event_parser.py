"""
event_parser.py — Game.log line parser for the SC Bridge

Parses relevant log lines and returns structured event dicts.
Each returned dict becomes a WebSocket frame broadcast to terminals.

Frame shape:
    {
        "event": "<event_id>",
        "ship":  "<ship_id_if_known>",
        "ts":    <ms_timestamp>,   # added by bridge.py
        "data":  { ... }
    }

Adding a new event
──────────────────
1. Add a new `elif` branch in `parse_log_line()`.
2. Use `_emit()` to build the frame.
3. Extract relevant fields from the regex or split.
4. Update sc_gamelink.h to define the matching SC_GAMELINK_EVT_* constant.
5. Register a handler in the relevant UI screen or component.

EAC Safety
──────────
- This module ONLY reads plain text from Game.log.
- No game process memory is accessed.
- No API hooks or debugger attachment is used.
"""

from __future__ import annotations

import re
from typing import Any

# ── Helpers ──────────────────────────────────────────────────────────────────

_active_ship: str = "unknown"


def _emit(event_id: str, data: dict[str, Any]) -> dict[str, Any]:
    return {
        "event": event_id,
        "ship": _active_ship,
        "data": data,
    }


# ── Pattern library ──────────────────────────────────────────────────────────
# Each pattern is a compiled regex. Capture groups are named.

_RE_SHIP_SPAWN = re.compile(
    r"<Vehicle id=\d+ class=(?P<ship>[A-Za-z0-9_]+) .+?SPAWNED"
)
_RE_GEAR_UP = re.compile(r"LandingSystem.*Retract")
_RE_GEAR_DOWN = re.compile(r"LandingSystem.*Deploy")
_RE_QUANTUM_SPOOL = re.compile(r"QuantumDrive.*Spooling")
_RE_QUANTUM_READY = re.compile(r"QuantumDrive.*Ready")
_RE_QUANTUM_JUMP = re.compile(r"QuantumDrive.*Jumping")
_RE_QUANTUM_COOLDOWN = re.compile(r"QuantumDrive.*Cooldown")
_RE_PLAYER_RESPAWN = re.compile(r"<Actor Death.*player=(?P<player>[^\s>]+)")
_RE_KILL = re.compile(r"<Vehicle Death.*attacker=(?P<attacker>[^\s>]+)")


# ── Main parser ───────────────────────────────────────────────────────────────

def parse_log_line(line: str) -> dict[str, Any] | None:
    """
    Parse a single Game.log line and return an event dict, or None
    if the line is not a recognised event.
    """
    global _active_ship

    # ── Ship spawn ───────────────────────────────────────────────────────
    m = _RE_SHIP_SPAWN.search(line)
    if m:
        _active_ship = m.group("ship").lower()
        return _emit("ship_spawned", {"ship_class": _active_ship})

    # ── Landing gear ─────────────────────────────────────────────────────
    if _RE_GEAR_UP.search(line):
        return _emit("gear_state_changed", {"state": "retracting"})
    if _RE_GEAR_DOWN.search(line):
        return _emit("gear_state_changed", {"state": "deploying"})

    # ── Quantum drive ────────────────────────────────────────────────────
    if _RE_QUANTUM_JUMP.search(line):
        return _emit("quantum_drive_state", {"state": "jumping", "spool_pct": 1.0})
    if _RE_QUANTUM_READY.search(line):
        return _emit("quantum_drive_state", {"state": "ready", "spool_pct": 1.0})
    if _RE_QUANTUM_SPOOL.search(line):
        return _emit("quantum_drive_state", {"state": "spooling", "spool_pct": 0.0})
    if _RE_QUANTUM_COOLDOWN.search(line):
        return _emit("quantum_drive_state", {"state": "cooldown", "spool_pct": 0.0})

    # ── Player death / respawn ───────────────────────────────────────────
    m = _RE_PLAYER_RESPAWN.search(line)
    if m:
        return _emit("player_respawned", {"player": m.group("player")})

    # ── Ship destroyed ───────────────────────────────────────────────────
    m = _RE_KILL.search(line)
    if m:
        return _emit("ship_destroyed", {"attacker": m.group("attacker")})

    return None
