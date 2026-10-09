// Each stdio client owns its baseline; never share deltas between agents.
export class StateResponses {
  reset() { this.previous = undefined; }
  apply(result, full = false) {
    const state = result.game_state;
    if (!state) return result;
    const previous = this.previous;
    this.previous = structuredClone(state);
    if (full || !previous || !state.available || previous.adapter !== state.adapter ||
        previous.map?.id !== state.map?.id || state.frame <= previous.frame) {
      return { ...result, state_format: 'full' };
    }
    const changes = {};
    for (const [key, value] of Object.entries(state)) {
      if (JSON.stringify(value) !== JSON.stringify(previous[key])) changes[key] = value;
    }
    const removed = Object.keys(previous).filter(key => !(key in state));
    const { game_state, ...metadata } = result;
    return { ...metadata, state_format: 'delta', base_frame: previous.frame,
      game_state_changes: changes, removed_state_fields: removed };
  }
}
