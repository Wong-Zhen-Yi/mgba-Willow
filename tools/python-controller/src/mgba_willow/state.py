"""Per-controller response views; full snapshots never leave the local baseline."""

from copy import deepcopy


def compact_state(state):
    keys = ("supported", "available", "adapter", "frame", "interaction", "map", "player",
            "movement_available", "battle_menu_ready", "menu", "battle")
    view = {key: state[key] for key in keys if key in state}
    if state.get("party") is not None:
        party = state["party"]
        members = party if isinstance(party, list) else party.get("members")
        fields = ("slot", "available", "reason", "species_id", "level", "hp", "max_hp", "status")
        view["party"] = ({"available": party.get("available", True) if isinstance(party, dict) else True,
                          "members": [{key: mon[key] for key in fields if key in mon} for mon in members]}
                         if members is not None else party)
    return view


class StateResponses:
    def __init__(self):
        self.previous = None

    def reset(self):
        self.previous = None

    def apply(self, result, full=False, compact=False):
        state = result.get("game_state")
        if state is None:
            return result
        previous = self.previous
        self.previous = deepcopy(state)
        metadata = {key: value for key, value in result.items() if key != "game_state"}
        if compact and not full and state.get("supported") and state.get("available"):
            events = state.get("recent_events", [])
            old_events = previous.get("recent_events", []) if previous and state.get("frame", 0) > previous.get("frame", 0) else []
            return {**metadata, "state_format": "compact", "game_state": compact_state(state),
                    "new_events": [event for event in events if event not in old_events]}
        if (full or state.get("interaction") in ("battle", "dialogue")
                or (state.get("menu") or {}).get("kind") not in (None, "none", "")
                or not previous or not state.get("available") or previous.get("adapter") != state.get("adapter")
                or (previous.get("map") or {}).get("id") != (state.get("map") or {}).get("id")
                or state.get("frame", 0) <= previous.get("frame", 0)):
            return {**result, "state_format": "full"}
        return {**metadata, "state_format": "delta", "base_frame": previous.get("frame"),
                "game_state_changes": {key: value for key, value in state.items() if key not in previous or value != previous[key]},
                "removed_state_fields": [key for key in previous if key not in state]}


def needs_image(state):
    state = state or {}
    menu = state.get("menu") or {}
    return (not state.get("available") or not state.get("supported")
            or (menu.get("kind") != "none" and not menu.get("input_ready"))
            or state.get("interaction") not in ("overworld", "battle"))


def handoff(state):
    if not state or not state.get("supported"):
        return {"available": False, "reason": "Unsupported game state"}
    events = state.get("recent_events", [])
    last_save = next((event for event in reversed(events) if event.get("type") == "save_confirmed"), None)
    result = {
        "available": state.get("available") is True,
        "location": {"map": state.get("map"), "player": state.get("player")},
        "story_progress": state.get("story_progress"), "party": state.get("party"),
        "inventory": state.get("inventory"), "last_observed_save": last_save,
        "save_evidence": ("Observed save-success callback; location reflects that historical frame, not necessarily current progress."
                          if last_save else "No save confirmation retained in this decoder session. This does not mean the game is unsaved."),
        "recent_events": events,
        "next_step": ("Inspect the active battle menu, battler HP, and selected target before input."
                      if state.get("interaction") == "battle" else
                      "Use the saved story notes and current flags to choose the next objective; record any remaining uncertainty."),
    }
    result.update({key: state[key] for key in ("frame", "interaction", "menu") if key in state})
    return result
