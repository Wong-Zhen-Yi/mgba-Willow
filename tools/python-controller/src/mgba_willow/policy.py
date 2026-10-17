"""Port of Willow's decision guards and bounded single wild battle routine."""

from copy import deepcopy
import re

from .bridge import WillowError

DIRECTIONS = {"Up", "Down", "Left", "Right"}


def input_problem(state, method, args):
    state = state or {}
    expected = args.get("expected_menu")
    if expected:
        if state.get("supported") is not True or state.get("available") is not True:
            return "Menu preconditions require available verified game state. Observe before pressing."
        for field, value in expected.items():
            observed = (state.get("menu") or {}).get(field, "unavailable")
            if type(observed) is not type(value) or observed != value:
                return f"Menu changed: expected {field}={value}, observed {observed}. Observe and decide again; no input sent."
    if state.get("supported") is not True:
        return None
    actions = ([{"buttons": [args["button"]], "frames": args.get("hold_frames", 2)}] if method == "press"
               else [args] if method == "act" else args["actions"])
    inputs = [action for action in actions if action["buttons"]]
    if not inputs:
        return None
    tap_frames = 2 if method == "press" else 1
    batch = len(inputs) > 1 or any(action["frames"] > tap_frames or len(action["buttons"]) > 1 for action in inputs)
    if any(set(action["buttons"]) & {"A", "B", "Start", "Select"} for action in inputs) and batch:
        return "Decision input requires one single-button tap. Use press, inspect the new prompt/menu, then decide again."
    if any(set(action["buttons"]) & DIRECTIONS for action in inputs):
        if state.get("interaction") == "overworld" and state.get("movement_available") is False:
            return "Movement is still restricted. Wait with no buttons and inspect readiness before moving."
        if state.get("interaction") != "overworld" and batch:
            return "Menu or animation input requires one single-button tap followed by inspection."
    return None


def boundary(state):
    state = state or {}
    return deepcopy([state.get("supported"), state.get("available"), state.get("interaction"),
                     (state.get("map") or {}).get("id"), state.get("menu"), state.get("recent_events")])


async def guarded_verified_input(request, method, args, before):
    problem = input_problem(before.get("game_state"), method, args)
    if problem:
        raise WillowError(problem)
    if method == "press":
        return await request(method, args)
    actions = [args] if method == "act" else args["actions"]
    steps, start_frame, reason = [], None, None
    initial = boundary(before.get("game_state"))
    last_position = deepcopy((before.get("game_state") or {}).get("player") or {})
    unchanged_frames = 0
    for action in actions:
        remaining, step_start = action["frames"], None
        while remaining > 0:
            frames = 1 if action["buttons"] else min(8, remaining)
            result = await request("act", {"buttons": action["buttons"], "frames": frames, "screenshot": False})
            if start_frame is None:
                start_frame = result.get("action_start_frame")
            if step_start is None:
                step_start = result.get("action_start_frame")
            if result.get("error"):
                return result
            if result.get("cancelled"):
                reason = "cancelled"
                break
            remaining -= frames
            state = result.get("game_state") or {}
            if boundary(state) != initial or result.get("human_keys"):
                reason = "human_input" if result.get("human_keys") else "decision_point"
                break
            position = state.get("player") or {}
            if set(action["buttons"]) & DIRECTIONS and all(type(position.get(key)) is int for key in ("x", "y")):
                unchanged_frames = unchanged_frames + frames if all(position[key] == last_position.get(key) for key in ("x", "y")) else 0
                last_position = deepcopy(position)
                if unchanged_frames >= 60:
                    reason = "position_unchanged"
                    break
            else:
                unchanged_frames = 0
            if action["buttons"] and state.get("interaction") == "overworld" and state.get("movement_available") is False and not position.get("moving"):
                reason = "movement_restricted"
                break
        if remaining == 0 and not result.get("cancelled"):
            steps.append({"action_start_frame": step_start, "action_end_frame": result.get("action_end_frame")})
        if reason:
            break
    if args.get("screenshot") is not False:
        result = {**result, **await request("observe", {"screenshot": True})}
    result = {**result, "action_start_frame": start_frame,
              "progress": {**result.get("progress", {}), "completed": not bool(reason),
                           "reason": reason or "frames_completed", "requires_inspection": bool(reason)}}
    if method == "act_sequence":
        result["steps"] = steps
    return result


def members(state):
    return (state.get("party") or {}).get("members") or []


def identity(state):
    return deepcopy([[mon.get("slot"), mon.get("species_id"), mon.get("level"),
                      [move.get("id") for move in mon["moves"]] if mon.get("moves") is not None else None]
                     for mon in members(state)])


def decision(state):
    state = state or {}
    return deepcopy([state.get(key) for key in ("supported", "available", "interaction", "menu", "battle", "party")]
                    + [(state.get("input") or {}).get("human_keys"), state.get("recent_events")])


async def train_battle(request, args):
    result = await request("get_game_state", {"screenshot": False})
    initial = identity(result.get("game_state") or {})
    initial_events = deepcopy((result.get("game_state") or {}).get("recent_events", []))
    actions, waits, checks, pending = 0, 0, 0, None

    def finish(reason):
        return {**result, "training": {"reason": reason, "actions": actions, "waited_frames": waits, "requires_inspection": True}}

    while actions < args.get("max_actions", 30) and waits < 600 and checks < 1200:
        checks += 1
        state = result.get("game_state") or {}
        if result.get("error"):
            return result
        if any(result.get(key) for key in ("cancelled", "paused", "human_keys")) or (state.get("input") or {}).get("human_keys"):
            return finish("interrupted")
        if not state.get("supported") or not state.get("available"):
            return finish("unavailable_state")
        if not (state.get("party") or {}).get("available") or not members(state) or any(mon.get("available") is False for mon in members(state)):
            return finish("unavailable_party")
        if any(event not in initial_events and re.search(r"fainted|trying to learn|wants to learn|stop learning|forget a move|evolv|evolution", event.get("text", ""), re.I)
               for event in state.get("recent_events", [])):
            return finish("training_exception")
        battle = state.get("battle") or {}
        battlers = battle.get("battlers") or []
        if any(mon.get("hp") == 0 for mon in members(state) + battlers):
            return finish("fainting")
        if identity(state) != initial:
            return finish("party_or_moves_changed")
        if state.get("interaction") != "battle":
            return finish("unexpected_state")
        if (not battle.get("available") or len(battlers) != 2
                or any(not mon.get("available") or mon.get("absent") or mon.get("status") or mon.get("volatile_status") for mon in battlers)
                or battle.get("type_flags") not in (0, 4) or battle.get("outcome")):
            return finish("unexpected_battle")
        menu = state.get("menu") or {}
        if menu.get("kind") == "none":
            pending = None
            result = await request("act", {"buttons": [], "frames": 1, "screenshot": False})
            waits += 1
            continue
        cursor = menu.get("cursor")
        if not menu.get("input_ready") or menu.get("kind") not in ("battle_action", "battle_move") or type(cursor) is not int or not 0 <= cursor <= 3:
            return finish("unexpected_menu")
        if pending == menu:
            return finish("tap_unverified")
        target = 0 if menu["kind"] == "battle_action" else args["move_slot"]
        if cursor == target:
            move = menu.get("selected_move") or {}
            if menu["kind"] == "battle_move" and not (move.get("id", 0) > 0 and move.get("pp", 0) > 0):
                return finish("move_unavailable")
            button = "A"
        elif cursor & 1 != target & 1:
            button = "Right" if target & 1 else "Left"
        else:
            button = "Down" if target & 2 else "Up"
        fresh = await request("get_game_state", {"screenshot": False})
        if any(fresh.get(key) for key in ("error", "cancelled", "paused", "human_keys")) or decision(fresh.get("game_state")) != decision(state):
            result = fresh
            continue
        pending = deepcopy(menu)
        result = await guarded_verified_input(request, "press", {"button": button, "screenshot": False,
                                              "expected_menu": {"kind": menu["kind"], "cursor": cursor}}, fresh)
        actions += 1
    return finish("budget")
