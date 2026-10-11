// Validate verified Emerald input before advancing any frames. Raw tools remain
// available for other games, whose decision boundaries we cannot decode.
export function inputProblem(state, method, args) {
  if (state?.supported !== true) return;
  const actions = method === 'press'
    ? [{ buttons: [args.button], frames: args.hold_frames ?? 1 }]
    : method === 'act' ? [args] : args.actions;
  const inputs = actions.filter(action => action.buttons.length);
  if (!inputs.length) return;
  const decision = action => action.buttons.some(button => ['A', 'B', 'Start', 'Select'].includes(button));
  if (inputs.some(decision) && (inputs.length > 1 || inputs.some(action => action.frames > 1 || action.buttons.length > 1))) {
    return 'Decision input requires one single-button tap. Use press, inspect the new prompt/menu, then decide again.';
  }
  const direction = action => action.buttons.some(button => ['Up', 'Down', 'Left', 'Right'].includes(button));
  if (inputs.some(direction)) {
    if (state.interaction === 'overworld' && state.movement_available === false) {
      return 'Movement is still restricted. Wait with no buttons and inspect readiness before moving.';
    }
    if (state.interaction !== 'overworld' && (inputs.length > 1 || inputs.some(action => action.frames > 1 || action.buttons.length > 1))) {
      return 'Menu or animation input requires one single-button tap followed by inspection.';
    }
  }
}

export async function guardedVerifiedInput(request, method, args, before) {
  const problem = inputProblem(before.game_state, method, args);
  if (problem) throw new Error(problem);
  if (method === 'press') return request(method, args);
  const actions = method === 'act' ? [args] : args.actions;
  const steps = [];
  let result, startFrame, reason;
  let lastPosition = before.game_state?.player, unchangedFrames = 0;
  const boundary = state => JSON.stringify([state?.supported, state?.available, state?.interaction,
    state?.map?.id, state?.menu, state?.recent_events]);
  const initialBoundary = boundary(before.game_state);
  for (const action of actions) {
    let remaining = action.frames, stepStart;
    while (remaining > 0) {
      // Check every input frame so a new menu cannot consume the rest of a hold.
      const frames = action.buttons.length ? 1 : Math.min(8, remaining);
      result = await request('act', { buttons: action.buttons, frames, screenshot: false });
      startFrame ??= result.action_start_frame;
      stepStart ??= result.action_start_frame;
      if (result.error) return result;
      if (result.cancelled) { reason = 'cancelled'; break; }
      remaining -= frames;
      if (boundary(result.game_state) !== initialBoundary || result.human_keys) {
        reason = result.human_keys ? 'human_input' : 'decision_point';
        break;
      }
      const state = result.game_state;
      const position = state?.player;
      if (action.buttons.some(button => ['Up', 'Down', 'Left', 'Right'].includes(button)) &&
          Number.isInteger(position?.x) && Number.isInteger(position?.y)) {
        unchangedFrames = position.x === lastPosition?.x && position.y === lastPosition?.y
          ? unchangedFrames + frames : 0;
        lastPosition = position;
        if (unchangedFrames >= 60) { reason = 'position_unchanged'; break; }
      } else unchangedFrames = 0;
      if (action.buttons.length && state?.interaction === 'overworld' &&
          state.movement_available === false && !state.player?.moving) {
        reason = 'movement_restricted'; break;
      }
    }
    if (remaining === 0 && !result.cancelled) steps.push({ action_start_frame: stepStart, action_end_frame: result.action_end_frame });
    if (reason) break;
  }
  // Encode one final screenshot, preserving actual action boundaries even if the
  // observation arrives later. Internal frame checks need only decoded state.
  if (args.screenshot !== false) result = { ...result, ...await request('observe', { screenshot: true }) };
  return { ...result, action_start_frame: startFrame,
    ...(method === 'act_sequence' ? { steps } : {}),
    progress: { ...result.progress, completed: !reason, reason: reason ?? 'frames_completed',
      requires_inspection: !!reason } };
}
