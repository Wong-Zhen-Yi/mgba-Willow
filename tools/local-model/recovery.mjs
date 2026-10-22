import { setTimeout as delay } from 'node:timers/promises';

// A bounded local spatial memory, not another full transcript sent on every turn.
export class RouteMemory {
  constructor() { this.visits = new Map(); this.positions = []; this.last = undefined; }
  observe(result) {
    const state = result.game_state;
    if (state.interaction !== 'overworld' || !state.movement_available) return;
    const key = this.key(state.map?.id, state.player);
    if (key === this.last) return;
    this.last = key;
    this.visits.set(key, (this.visits.get(key) ?? 0) + 1);
    if (this.visits.size > 2048) this.visits.delete(this.visits.keys().next().value);
    this.positions.push(key);
    if (this.positions.length > 16) this.positions.shift();
  }
  key(map, point) { return `${map}:${point?.x},${point?.y}`; }
  looping() { return this.positions.filter(key => key === this.last).length >= 4; }
  describe(result, candidates) {
    return candidates.filter(action => action.method === 'move_to').map(action => ({
      action: action.id, visits: this.visits.get(this.key(result.game_state.map?.id, action.args)) ?? 0,
    }));
  }
  escape(result, candidates) {
    // Only choose an unvisited, already validated adjacent tile. Never override
    // model stop/confirmation policy or force a guessed route through a wall.
    const unseen = this.describe(result, candidates).find(item => item.visits === 0);
    if (!unseen) return;
    return candidates.find(action => action.id === unseen.action);
  }
}

export function observationFor(result, goal, history, memory, guidance) {
  const { quest_flags, rom_sha1, normalized_rom_sha1, ...state } = result.game_state;
  return { goal, game_state: state, local_map: result.local_map,
    recent_actions: history, route_memory: memory.describe(result, result.candidates ?? []),
    ...(guidance ? { supervisor_guidance: guidance } : {}),
    rules: 'One action then inspect. Prefer progress toward the goal; avoid revisiting tiles without a reason. Choice scores rank alternatives, not success rates. Allow trainer and wild battles. Check live HP/PP and targets. Unknown menus require help. Stop for verified goal completion or a safe in-game save; checkpoints are not saves. No visual information is available.' };
}

export async function chooseWithRetry(model, state, candidates, signal, log) {
  for (let attempt = 0; ; attempt++) {
    try { return await model.choose(state, candidates, signal); }
    catch (error) {
      const transient = error.retryable || error.name === 'TimeoutError' || error instanceof TypeError;
      if (signal.aborted || !transient || attempt >= 2) throw error;
      await log({ type: 'model_retry', attempt: attempt + 1, message: error.message });
      // Only inference is retried, NEVER emulator inputs or bridge failures.
      await delay(250 * 2 ** attempt, undefined, { signal });
    }
  }
}

export function validateChoice(choice, candidates) {
  if (!candidates.some(action => action.id === choice?.id) ||
      !Number.isFinite(choice.probability) || choice.probability < 0 || choice.probability > 1) {
    throw new Error('Invalid model decision');
  }
  if (!choice.probabilities) return; // Legacy/scorer adapters retain the strict fixed gate.
  const weights = choice.probabilities;
  const ids = candidates.map(action => action.id);
  if (Object.keys(weights).length !== ids.length || ids.some(id => !Number.isFinite(weights[id]) || weights[id] < 0 || weights[id] > 1) ||
      Math.abs(ids.reduce((sum, id) => sum + weights[id], 0) - 1) > 0.02 ||
      weights[choice.id] !== choice.probability || choice.probability < Math.max(...Object.values(weights))) {
    throw new Error('Invalid model decision distribution');
  }
  return ids.length;
}
