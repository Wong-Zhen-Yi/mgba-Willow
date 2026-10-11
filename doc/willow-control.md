# Willow gameplay control and verification

Read the objective tracker and latest story notes before gameplay, then compare
them with a fresh full observation. Interpret the state before sending input.

## One action, then inspect

At dialogue, battle, switch, evolution, move-learning, item, and save prompts,
send one `press` and inspect the returned state and screenshot before confirming
again. A new prompt is a new decision even when it looks like ordinary dialogue.
Do not batch A/B presses or combine movement and confirmation. Verified Emerald
rejects those batches before advancing frames. Batch only clear travel or waits;
raw travel stops when decoded interaction, map, menu, or message evidence changes.
Use `move_to` for efficient verified travel rather than long raw holds.

## Retain the complete state

Keep a baseline for this MCP client and process every tool response. A full
response replaces the baseline. A delta replaces changed **top-level** fields
and deletes `removed_state_fields`; never deep-merge battlers, party, or menus.
Verify `base_frame` matches the baseline frame; refresh with `full_state:true`
after missing responses or a mismatch. Missing fields do not mean zero HP.
Battle, dialogue, and decoded menu responses now carry full snapshots.

Before an attack or switch, inspect live battler HP/status, side/position, party
index, cursor, selected move, and target. Party HP can differ from live battler
HP during battle. If HP or menu state looks inconsistent, request a fresh full
observation with a screenshot and reconcile it before input. Only `atomic:true`
certifies screenshot and state frame alignment.

## Plan from the live map

Inspect collision, elevation, occupancy, tile behavior, exits, and trainer risk
before choosing a destination. Unknown tiles and no-path results require a new
route, not repeated directional guesses. Gym holes, geysers, and forced movement
need explicit observation and short controls; a visible hole does not prove an
active warp. Refresh the map after any transition or unexpected displacement.

`move_to` avoids potential trainer sightlines by default and rechecks the next
edge while moving. This conservative rule covers all four cardinal rays within
each active trainer's range, including possible rotation. It does not decode
defeat flags or prove that walls block sight; defeated trainers can cause a
detour or no-path result. Use `avoid_trainers:false` only after inspecting the
route and deciding that a battle or this potential exposure is acceptable.
Replan after `trainer_sightline`, blockage, encounters, or any incomplete route.

## Readiness and animation

`movement_available` and `overworld_ready` require a stable player, no single or
held movement animation, no forced-movement flag, no script lock, and no fade.
A completed wait does not prove that a transition occurred or that a requested
step will succeed. Check the map ID, then use one verified route segment or short
step and inspect actual coordinate progress. During geyser or hole sequences,
wait with no buttons until movement is available; never use repeated directions
to force an animation to finish.

## Save and record evidence

After each verified milestone, immediately update `POKEMON_EMERALD_OBJECTIVES.md`
and `STORY_PROGRESS_NOTES.md` with the outcome, message/flag evidence, frame/map/
position, party condition, supplies, and next objective. Keep uncertain progress
explicit; location alone does not establish completion.

During long sessions, make an in-game save at safe points after major objectives,
healing, or substantial preparation. Inspect each save prompt separately, wait
through writing, and verify `save_confirmed` or the save-success screenshot.
Record its evidence and location immediately, before bounded event history can
discard it. A checkpoint is additional retry protection, not an in-game save.
At handoff, obtain `get_handoff`, reconcile the latest save with current progress,
write the next objective and uncertainties, then disconnect.
