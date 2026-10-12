import test from 'node:test';
import assert from 'node:assert/strict';
import { StateResponses } from '../state-responses.mjs';

const state = (frame, hp = 20, map = 1) => ({ supported: true, available: true, adapter: 'emerald_en_v1', frame,
  map: { id: map }, player: { x: 1, y: 2 }, party: [{ hp }], inventory: { items: [] } });

test('compact decisions omit bulky fields, retain a full local baseline and only report new events', () => {
  const responses = new StateResponses();
  const first = { ...state(1), interaction: 'battle', menu: { kind: 'battle_move', selected_move: { id: 1, pp: 5 } },
    battle: { battlers: [{ hp: 7, status: 1 }], target: 1 }, quest_flags: { ids: [1, 2] },
    recent_events: [{ frame: 1, text: 'Ready' }] };
  const compact = responses.apply({ game_state: first }, false, true);
  assert.equal(compact.state_format, 'compact');
  assert.equal(compact.game_state.inventory, undefined);
  assert.equal(compact.game_state.quest_flags, undefined);
  assert.deepEqual(compact.game_state.battle, first.battle);
  assert.deepEqual(responses.previous, first);
  const next = { ...first, frame: 2, recent_events: [...first.recent_events, { frame: 2, text: 'Attack' }] };
  assert.deepEqual(responses.apply({ game_state: next }, false, true).new_events, [{ frame: 2, text: 'Attack' }]);
  assert.deepEqual(responses.apply({ game_state: next }, true, true).game_state, next);
  responses.reset();
  assert.equal(responses.apply({ game_state: next }, false, true).new_events.length, 2);
});

test('compact state responses have isolated, reconstructible per-client baselines', () => {
  const a = new StateResponses(), b = new StateResponses();
  assert.equal(a.apply({ game_state: state(1) }).state_format, 'full');
  const delta = a.apply({ game_state: state(2, 15), png: 'image', atomic: true });
  assert.equal(delta.state_format, 'delta');
  assert.equal(delta.base_frame, 1);
  assert.deepEqual(delta.game_state_changes, { frame: 2, party: [{ hp: 15 }] });
  assert.equal(delta.png, 'image'); assert.equal(delta.atomic, true);
  assert.equal(b.apply({ game_state: state(2) }).state_format, 'full');
  assert.deepEqual({ ...state(1), ...delta.game_state_changes }, state(2, 15));
  assert.equal(a.apply({ game_state: state(3) }, true).state_format, 'full');
  assert.equal(a.apply({ game_state: state(4, 20, 2) }).state_format, 'full');
  assert.equal(a.apply({ game_state: state(1, 20, 2) }).state_format, 'full');
  a.reset(); assert.equal(a.apply({ game_state: state(5) }).state_format, 'full');
});

test('unavailable and unsupported ROM states remain explicit and raw results remain intact', () => {
  const responses = new StateResponses();
  const raw = { hex: '0102' };
  assert.equal(responses.apply(raw), raw);
  for (const game_state of [{ supported: false }, { supported: true, available: false }]) {
    assert.deepEqual(responses.apply({ game_state }).game_state, game_state);
  }
});

test('deltas explicitly list removed state fields and do not retain mutable references', () => {
  const responses = new StateResponses();
  const original = state(1); responses.apply({ game_state: original }); original.party[0].hp = 99;
  const next = state(2); delete next.inventory;
  const delta = responses.apply({ game_state: next });
  assert.deepEqual(delta.game_state_changes, { frame: 2 });
  assert.deepEqual(delta.removed_state_fields, ['inventory']);
});

test('battle and menu decisions always carry complete HP and cursor context', () => {
  const responses = new StateResponses();
  responses.apply({ game_state: state(1) });
  for (const [index, context] of [{ interaction: 'battle', battle: { battlers: [{ hp: 7 }] } },
    { interaction: 'dialogue' }, { menu: { kind: 'party', selected_party_slot: 2 } }].entries()) {
    const snapshot = { ...state(index + 2, 7), ...context };
    const result = responses.apply({ game_state: snapshot });
    assert.equal(result.state_format, 'full');
    assert.deepEqual(result.game_state, snapshot);
    assert.equal(result.game_state_changes, undefined);
  }
});
