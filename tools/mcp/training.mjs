import { guardedVerifiedInput } from './control-policy.mjs';

const members = state => state?.party?.members ?? [];
const identity = state => JSON.stringify(members(state).map(mon => [mon.slot, mon.species_id, mon.level,
  mon.moves?.map(move => move.id)]));
const decision = state => JSON.stringify([state?.supported, state?.available, state?.interaction,
  state?.menu, state?.battle, state?.party, state?.input?.human_keys, state?.recent_events]);

// Deliberately narrow: the caller chooses the move, and prompts remain human/model decisions.
export async function trainBattle(request, args) {
  let result = await request('get_game_state', { screenshot: false });
  const initial = identity(result.game_state);
  const initialEvents = new Set((result.game_state?.recent_events ?? []).map(event => JSON.stringify(event)));
  let actions = 0, waits = 0, checks = 0, pending;
  const finish = reason => ({ ...result, training: { reason, actions, waited_frames: waits,
    requires_inspection: true } });
  while (actions < (args.max_actions ?? 30) && waits < 600 && checks++ < 1200) {
    const state = result.game_state;
    if (result.error) return result;
    if (result.cancelled || result.paused || result.human_keys || state?.input?.human_keys) return finish('interrupted');
    if (!state?.supported || !state.available) return finish('unavailable_state');
    if (!state.party?.available || !members(state).length || members(state).some(mon => mon.available === false)) return finish('unavailable_party');
    if ((state.recent_events ?? []).some(event => !initialEvents.has(JSON.stringify(event)) &&
        /fainted|trying to learn|wants to learn|stop learning|forget a move|evolv|evolution/i.test(event.text ?? ''))) {
      return finish('training_exception');
    }
    if (members(state).some(mon => mon.hp === 0) || state.battle?.battlers?.some(mon => mon.hp === 0)) return finish('fainting');
    if (identity(state) !== initial) return finish('party_or_moves_changed');
    if (state.interaction !== 'battle') return finish('unexpected_state');
    const battlers = state.battle?.battlers;
    if (!state.battle?.available || battlers?.length !== 2 ||
        battlers.some(mon => !mon.available || mon.absent || mon.status || mon.volatile_status) ||
        // BATTLE_TYPE_IS_MASTER (4) is set in ordinary non-link battles.
        // Source: pret/pokeemerald include/constants/battle.h. Reject all other bits.
        ![0, 4].includes(state.battle.type_flags) || state.battle.outcome) {
      return finish('unexpected_battle');
    }
    const menu = state.menu;
    // Unknown prompts (including move learning and evolution) are never acknowledged.
    if (menu?.kind === 'none') {
      if (pending) pending = undefined;
      result = await request('act', { buttons: [], frames: 1, screenshot: false });
      waits++;
      continue;
    }
    if (!menu?.input_ready || !['battle_action', 'battle_move'].includes(menu.kind) ||
        !Number.isInteger(menu.cursor) || menu.cursor < 0 || menu.cursor > 3) return finish('unexpected_menu');
    const signature = JSON.stringify(menu);
    if (pending === signature) return finish('tap_unverified');
    const target = menu.kind === 'battle_action' ? 0 : args.move_slot;
    let button;
    if (menu.cursor === target) {
      if (menu.kind === 'battle_move' && !(menu.selected_move?.id > 0 && menu.selected_move.pp > 0)) return finish('move_unavailable');
      button = 'A';
    } else if ((menu.cursor & 1) !== (target & 1)) button = target & 1 ? 'Right' : 'Left';
    else button = target & 2 ? 'Down' : 'Up';
    // Recheck this exact menu immediately before each individual decision.
    const fresh = await request('get_game_state', { screenshot: false });
    if (fresh.error || fresh.cancelled || fresh.paused || fresh.human_keys || decision(fresh.game_state) !== decision(state)) {
      result = fresh;
      continue;
    }
    pending = signature;
    result = await guardedVerifiedInput(request, 'press', { button, screenshot: false,
      expected_menu: { kind: menu.kind, cursor: menu.cursor } }, fresh);
    actions++;
  }
  return finish('budget');
}
