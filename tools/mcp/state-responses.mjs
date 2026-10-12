// Each stdio client owns its baseline; never share deltas between agents.
export class StateResponses {
  reset() { this.previous = undefined; }
  apply(result, full = false, compact = false) {
    const state = result.game_state;
    if (!state) return result;
    const previous = this.previous;
    this.previous = structuredClone(state);
    if (compact && !full && state.supported && state.available) {
      const events = state.recent_events ?? [];
      const oldEvents = previous && state.frame > previous.frame ? previous.recent_events ?? [] : [];
      const { game_state, ...metadata } = result;
      return { ...metadata, state_format: 'compact', game_state: compactState(state),
        new_events: events.filter(event => !oldEvents.some(old => JSON.stringify(old) === JSON.stringify(event))) };
    }
    // Decision snapshots retain complete battle/menu context, including HP and
    // selected targets, without relying on a consumer's earlier delta history.
    if (full || state.interaction === 'battle' || state.interaction === 'dialogue' ||
        (state.menu?.kind && state.menu.kind !== 'none') ||
        !previous || !state.available || previous.adapter !== state.adapter ||
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

// A self-contained decision view, not a delta. The complete snapshot stays local.
export function compactState(state) {
  const keys = ['supported', 'available', 'adapter', 'frame', 'interaction', 'map', 'player',
    'movement_available', 'battle_menu_ready', 'menu', 'battle'];
  const view = Object.fromEntries(keys.filter(key => key in state).map(key => [key, state[key]]));
  if (state.party) {
    const members = Array.isArray(state.party) ? state.party : state.party.members;
    const fields = ['slot', 'available', 'reason', 'species_id', 'level', 'hp', 'max_hp', 'status'];
    view.party = members ? { available: state.party.available ?? true, members: members.map(mon =>
      Object.fromEntries(fields.filter(key => key in mon).map(key => [key, mon[key]]))) } : state.party;
  }
  return view;
}
