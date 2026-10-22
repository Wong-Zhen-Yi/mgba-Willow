import { guardedVerifiedInput } from '../mcp/control-policy.mjs';
import { handoff } from '../mcp/handoff.mjs';
import { probabilityPolicy } from './decision-policy.mjs';
import { RouteMemory, observationFor, chooseWithRetry, validateChoice } from './recovery.mjs';

const stop = { id: 'stop', description: 'Stop and hand control back: goal reached, uncertain prompt, or help/save needed.' };
const wait = { id: 'wait', description: 'Wait eight frames with no buttons for an animation or text to finish.',
  method: 'act', args: { buttons: [], frames: 8 } };
const press = (button, description, menu) => ({ id: `press_${button}`, description, method: 'press',
  args: { button, ...(menu ? { expected_menu: { kind: menu.kind, cursor: menu.cursor } } : {}) } });

export function interruption(result) {
  if (result.error) return result.error;
  if (result.cancelled || result.paused || result.human_keys || result.game_state?.input?.human_keys) return 'human_interruption';
  if (result.connected_clients > 1) return 'other_agent_connected';
  if (!result.game_state?.supported || !result.game_state.available || result.game_state.adapter !== 'emerald_en_v1') return 'unsupported_or_unavailable_state';
}

// Deliberately bounded initial adapter. No blind input on unknown ROMs or menus.
export function candidatesFor(result) {
  if (interruption(result)) return [stop];
  const state = result.game_state, menu = state.menu;
  if ((state.recent_events ?? []).some(event => /trying to learn|wants to learn|forget a move|stop learning|evolv/i.test(event.text ?? ''))) return [stop];
  if (state.interaction === 'overworld' && menu?.kind === 'none') {
    if (!state.movement_available) return [stop, wait];
    const map = result.local_map, player = state.player;
    if (!map?.available || map.map_id !== state.map?.id || !player) return [stop];
    const moves = (map.tiles ?? []).filter(tile => tile.known && !tile.occupied &&
      !tile.special_traversal && tile.collision === 0 && tile.x >= 0 && tile.y >= 0 &&
      Math.abs(tile.x - player.x) + Math.abs(tile.y - player.y) === 1);
    return [stop, ...moves.map(tile => ({ id: `move_${tile.x}_${tile.y}`,
      description: `Walk to adjacent live tile (${tile.x},${tile.y}); behavior=${tile.behavior}, trainer_risk=${tile.trainer_risk}. Encounters are allowed.`,
      method: 'move_to', args: { x: tile.x, y: tile.y, max_frames: 120, avoid_trainers: false } })),
    press('A', 'Interact once with the tile/NPC immediately in front of the player.')];
  }
  if (state.interaction === 'dialogue' && menu?.kind === 'none') {
    // The decoder cannot certify yes/no or replacement submenu selections.
    return [stop];
  }
  if (state.interaction !== 'battle' || !state.battle?.available) return [stop];
  if (menu?.kind === 'none') return [stop, wait];
  if (!menu?.input_ready || !['battle_action', 'battle_move', 'battle_target'].includes(menu.kind)) return [stop];
  if (menu.kind === 'battle_target') {
    return [stop, ...(menu.target_available && menu.target?.side === 'opponent'
      ? [press('A', 'Confirm the currently decoded opponent target.', menu)] : [])];
  }
  if (!Number.isInteger(menu.cursor) || menu.cursor < 0 || menu.cursor > 3) return [stop];
  const directions = ['Up', 'Down', 'Left', 'Right'].map(button =>
    press(button, `Move the ${menu.kind} cursor one step ${button.toLowerCase()}; inspect before confirming.`, menu));
  // Do not flee or choose an unverified Bag/Party submenu.
  const confirm = menu.kind === 'battle_action' ? menu.cursor === 0
    : menu.selected_move?.id > 0 && menu.selected_move.pp > 0;
  return [stop, ...directions, ...(confirm ? [press('A', menu.kind === 'battle_action'
    ? 'Select Fight.' : `Use the selected move ${menu.selected_move.name ?? menu.selected_move.id} (PP ${menu.selected_move.pp}).`, menu)] : [])];
}

// Exclude clock/frame counters; include every decision-relevant decoded field.
export function decisionKey(result) {
  const { frame, input, ...state } = result.game_state ?? {};
  if (state.recent_events) state.recent_events = state.recent_events.map(({ frame, ...event }) => event);
  return JSON.stringify(state);
}

export async function play({ request, model, luna, goal, signal, maxSteps = 10000, maxMs = 43200000,
  maxAssists = 20, minProbability, minNavigationProbability, log = async () => {} }) {
  if (typeof goal !== 'string' || !goal.trim() || goal.length > 512) throw new Error('Goal must contain 1–512 characters');
  if (!Number.isInteger(maxSteps) || maxSteps < 1 || maxSteps > 100000 ||
      !Number.isInteger(maxAssists) || maxAssists < 0 || maxAssists > 100 ||
      !Number.isFinite(maxMs) || maxMs <= 0 || maxMs > 2147483647 || [minProbability, minNavigationProbability].some(value =>
        value !== undefined && (!Number.isFinite(value) || value < 0 || value > 1))) {
    throw new Error('Invalid local-player limits');
  }
  const budget = AbortSignal.timeout(maxMs);
  const active = signal ? AbortSignal.any([signal, budget]) : budget;
  let result, reason = 'step_budget', details, steps = 0, repeats = 0, previousKey;
  let pendingConfirm, pendingFrames = 0, animationFrames = 0, assists = 0, guidance, guidanceUntil = 0;
  const history = [], memory = new RouteMemory(), assistedKeys = new Set();
  const observe = candidates => observationFor({ ...result, candidates }, goal, history, memory,
    steps < guidanceUntil ? guidance : undefined);
  const assist = async (cause, candidates, key) => {
    // Bound paid calls and never repeatedly consult on an identical stuck state.
    if (!luna || assists >= maxAssists || assistedKeys.has(key) || active.aborted) return;
    assistedKeys.add(key);
    assists++;
    await log({ type: 'assistance_requested', step: steps, reason: cause, attempt: assists });
    try {
      const answer = await luna.choose({ ...observe(candidates), stuck_reason: cause }, candidates, active);
      const action = candidates.find(candidate => candidate.id === answer?.id);
      if (!action) throw new Error('Luna selected an unsupported action');
      guidance = typeof answer.guidance === 'string' ? answer.guidance.slice(0, 2000) : undefined;
      guidanceUntil = steps + 12;
      await log({ type: 'assistance', step: steps, reason: cause, action: action.id, guidance });
      return action.id === 'stop' ? undefined : action;
    } catch (error) {
      await log({ type: 'assistance_error', step: steps, message: error.message });
      // Advice failure cannot turn into an input or an infinite retry loop.
    }
  };
  try {
    await request('set_objective', { text: goal });
    for (; steps < maxSteps; steps++) {
      if (active.aborted) { reason = signal?.aborted ? 'cancelled' : 'time_budget'; break; }
      result = await request('get_local_map', { radius: 2, screenshot: false });
      if ((reason = interruption(result))) break;
      const key = decisionKey(result);
      repeats = key === previousKey ? repeats + 1 : 0;
      previousKey = key;
      // A confirmation is never repeated if it has not changed decoded state.
      if (pendingConfirm === key) {
        if (pendingFrames >= 120) {
          reason = 'unverified_confirmation';
          await assist(reason, [stop], key);
          break;
        }
        result = await request('act', { buttons: [], frames: 8, screenshot: false });
        pendingFrames += 8;
        if ((reason = interruption(result))) break;
        reason = 'step_budget';
        continue;
      }
      pendingConfirm = undefined;
      pendingFrames = 0;
      const candidates = candidatesFor(result);
      if (candidates.length === 1) {
        reason = 'needs_human_inspection';
        await assist(reason, candidates, key);
        break;
      }
      // Animation is not a model decision. Eight identical observations used to
      // terminate battles after only 64 frames. Wait up to 1800 frames instead.
      if (candidates.length === 2 && candidates[1].id === 'wait') {
        if (animationFrames >= 1800) {
          reason = 'stalled';
          await assist(reason, [stop], key);
          break;
        }
        result = await request('act', { ...wait.args, screenshot: false });
        animationFrames += 8;
        await log({ type: 'action', step: steps, source: 'animation', action: 'wait', result });
        if ((reason = interruption(result))) break;
        reason = 'step_budget';
        continue;
      }
      animationFrames = 0;
      memory.observe(result);
      let action, source = 'kev';
      let stuck = repeats >= 8 ? 'stalled' : memory.looping() ? 'navigation_loop' : undefined;
      if (stuck) {
        // A proven unseen adjacent tile can break a spatial loop without an API
        // call; unchanged inputs/prompts instead require evidence-based advice.
        action = stuck === 'navigation_loop' ? memory.escape(result, candidates) : undefined;
        if (action) source = 'route_recovery';
      } else {
        let choice;
        try { choice = await chooseWithRetry(model, observe(candidates), candidates, active, log); }
        catch (error) {
          if (active.aborted) throw error;
          if (!luna) throw error;
          stuck = 'model_unavailable';
          await log({ type: 'model_error', step: steps, message: error.message });
        }
        if (active.aborted) { reason = signal?.aborted ? 'cancelled' : 'time_budget'; break; }
        if (!stuck) {
          const candidateCount = validateChoice(choice, candidates);
          action = candidates.find(candidate => candidate.id === choice.id);
          const policy = probabilityPolicy(action, { minProbability, minNavigationProbability, candidateCount });
          await log({ type: 'decision', step: steps, frame: result.frame, choice, action,
            policy: { ...policy, accepted: choice.probability >= policy.min_probability },
            handoff: handoff(result.game_state) });
          if (action.id === 'stop' || choice.probability < policy.min_probability) {
            stuck = action.id === 'stop' ? 'model_stop' : 'low_probability';
            details = { action: action.id, probability: choice.probability, ...policy };
            action = undefined;
          }
        }
      }
      if (!action && stuck) {
        reason = stuck;
        action = await assist(stuck, candidates, key);
        if (!action) break;
        source = 'luna';
      }
      // The emulator keeps running during either model's inference. All sources
      // use the same fresh-state and native movement/menu preconditions.
      const fresh = await request('get_game_state', { screenshot: false });
      if ((reason = interruption(fresh))) { result = fresh; break; }
      if (decisionKey(fresh) !== key) { result = fresh; reason = 'step_budget'; continue; }
      if (active.aborted) { reason = signal?.aborted ? 'cancelled' : 'time_budget'; break; }
      if (action.args.button === 'A') pendingConfirm = key;
      result = action.method === 'press'
        ? await guardedVerifiedInput(request, 'press', { ...action.args, screenshot: false }, fresh)
        : await request(action.method, { ...action.args, screenshot: false });
      history.push({ action: action.id, source, progress: result.progress, frame: result.frame });
      if (history.length > 12) history.shift();
      await log({ type: 'action', step: steps, source, action: action.id, result });
      if ((reason = interruption(result))) break;
      reason = 'step_budget';
    }
  } catch (error) {
    if (!active.aborted) { reason = 'error'; throw error; }
    reason = signal?.aborted ? 'cancelled' : 'time_budget';
  } finally {
    if (active.aborted) reason = signal?.aborted ? 'cancelled' : 'time_budget';
    // Retain fresh save/milestone evidence, never claim the goal or save succeeded.
    try { result = await request('get_game_state', { screenshot: false }); } catch {}
    details = { ...details, assists, guidance, interaction: result?.game_state?.interaction, menu: result?.game_state?.menu?.kind };
    await log({ type: 'handoff', reason: reason ?? 'error', details, steps, goal,
      handoff: handoff(result?.game_state), game_state: result?.game_state });
  }
  return { reason, steps, details };
}
