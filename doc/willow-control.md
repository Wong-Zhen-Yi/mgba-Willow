# Willow gameplay control and verification

Read one current handoff before gameplay and compare it with a fresh observation.
Consult the objective tracker or older story notes only to resolve missing context.
Interpret the state before sending input.

## One action, then inspect

At dialogue, battle, switch, evolution, move-learning, item, and save prompts,
send one `press` and inspect the returned state before confirming
again. A new prompt is a new decision even when it looks like ordinary dialogue.
Do not batch A/B presses or combine movement and confirmation. Verified Emerald
rejects those batches before advancing frames. Batch only clear travel or waits;
raw travel stops when decoded interaction, map, menu, or message evidence changes.
Use `move_to` for efficient verified travel rather than long raw holds.

## Retain the complete state

The MCP server retains the complete state locally. Default `state_format:compact`
responses are self-contained decision views: location, readiness, menu, party
HP/status, live battle state, selected move/target, and `new_events`. Replace the
previous compact view; never merge it into a full baseline. Flags and inventory
are available on demand with `full_state:true`. `compact:false` opts into the
legacy full/delta protocol described below. Both text and structured output use
the compact view, so hidden structured payloads do not resend the full snapshot.

Decoded states omit screenshots by default. Unfamiliar prompts and unavailable
state trigger an image; `screenshot:false` explicitly suppresses it. Request
`screenshot:true` for puzzles, special traversal, or discrepancies. A reliable
tap releases input for two frames, holds for two, and releases for two. Never
repeat a confirmation just because its effect is not yet visible; wait and inspect.

Keep a baseline for this MCP client and process every tool response. A full
response replaces the baseline. A delta replaces changed **top-level** fields
and deletes `removed_state_fields`; never deep-merge battlers, party, or menus.
Verify `base_frame` matches the baseline frame; refresh with `full_state:true`
after missing responses or a mismatch. Missing fields do not mean zero HP.
Legacy battle, dialogue, and decoded menu responses carry full snapshots.

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

For this playthrough, allow trainer and wild encounters during normal travel.
Use `move_to` with `avoid_trainers:false` after inspecting the live route; the
tool otherwise avoids potential trainer sightlines by default. Do not detour
around trainers or encounter terrain, buy or use Repels, or flee battles solely
to avoid encounters or shorten travel. Handle encounters as part of the
playthrough, inspecting each battle prompt before acting. If the party needs
recovery, heal and replenish supplies before continuing.

Movement still stops on encounters and other decision points. Inspect the new
state, handle the battle or interaction, then replan after `trainer_sightline`,
blockage, encounters, or any incomplete route.

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

Keep a short `CURRENT_HANDOFF.md` with the current objective, latest verified
milestone, frame/map/position, party condition, last save evidence, and next step.
Update it at milestones and at handoff; do not repeatedly load the full history.

## Bounded training

`train_battle(move_slot:0..3,max_actions:1..100)` repeats a caller-selected move
only in a decoded single wild battle. It verifies every cursor before one tap,
uses no-button frames for animations, and stops on fainting, changed party/moves,
unknown prompts (including move learning/evolution), status effects, human input,
cancelled actions, an unverified tap, or budget exhaustion. Inspect its final
state and `training.reason`. It never confirms replacement prompts, navigates
training routes, or retries confirmations. Use verified `move_to` segments for
travel between battles and `wait_until` for known animation conditions.
