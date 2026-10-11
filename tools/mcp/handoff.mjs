// Pure snapshot formatting: never advances the emulator or writes user notes.
export function handoff(state) {
  if (!state?.supported) return { available: false, reason: 'Unsupported game state' };
  const events = state.recent_events ?? [];
  const lastSave = events.findLast(event => event.type === 'save_confirmed');
  return {
    available: state.available === true,
    frame: state.frame,
    location: { map: state.map ?? null, player: state.player ?? null },
    interaction: state.interaction,
    menu: state.menu,
    story_progress: state.story_progress ?? null,
    party: state.party ?? null,
    inventory: state.inventory ?? null,
    last_observed_save: lastSave ?? null,
    save_evidence: lastSave ? 'Observed save-success callback; location reflects that historical frame, not necessarily current progress.'
      : 'No save confirmation retained in this decoder session. This does not mean the game is unsaved.',
    recent_events: events,
    next_step: state.interaction === 'battle' ? 'Inspect the active battle menu, battler HP, and selected target before input.'
      : 'Use the saved story notes and current flags to choose the next objective; record any remaining uncertainty.',
  };
}
