import test from 'node:test';
import assert from 'node:assert/strict';
import { trainBattle } from '../training.mjs';

const state = () => ({ supported: true, available: true, frame: 1, interaction: 'battle',
  menu: { kind: 'battle_move', input_ready: true, cursor: 0, selected_move: { id: 1, pp: 5 } },
  party: { available: true, members: [{ slot: 0, hp: 20, species_id: 1, level: 5, moves: [{ id: 1 }] }] },
  battle: { available: true, type_flags: 4, outcome: 0,
    battlers: [{ available: true, hp: 20 }, { available: true, hp: 10 }] } });

test('training verifies a move and stops at fainting without confirming again', async () => {
  let game_state = state();
  const calls = [];
  const result = await trainBattle(async (method, args) => {
    calls.push({ method, args });
    if (method === 'press') game_state = { ...game_state, battle: { ...game_state.battle,
      battlers: [{ available: true, hp: 20 }, { available: true, hp: 0 }] } };
    return { game_state };
  }, { move_slot: 0 });
  assert.equal(result.training.reason, 'fainting');
  assert.deepEqual(calls.map(call => call.method), ['get_game_state', 'get_game_state', 'press']);
  assert.equal(calls.at(-1).args.button, 'A');
});

test('ordinary wild master flag is accepted while trainer, double, link and special flags stop', async () => {
  for (const type_flags of [1, 2, 5, 6, 8, 12, 16, 128, 260, undefined]) {
    let calls = 0;
    const result = await trainBattle(async () => {
      calls++;
      const current = state(); current.battle.type_flags = type_flags;
      return { game_state: current };
    }, { move_slot: 0 });
    assert.equal(result.training.reason, 'unexpected_battle');
    assert.equal(calls, 1);
  }
});

test('training stops on unknown prompts, trainer battles, human input and unavailable PP', async () => {
  for (const change of [{ menu: { kind: 'party' } }, { human_keys: 1 },
    { battle: { ...state().battle, type_flags: 8 } },
    { menu: { ...state().menu, selected_move: { id: 1, pp: 0 } } }]) {
    let calls = 0;
    const { human_keys, ...fields } = change;
    const result = await trainBattle(async () => { calls++; return { game_state: { ...state(), ...fields }, human_keys }; },
      { move_slot: 0 });
    assert.equal(calls, 1);
    assert.notEqual(result.training.reason, 'budget');
  }
});

test('unchanged tap is not retried and refreshed menu is re-evaluated', async () => {
  let presses = 0, frame = 0;
  const result = await trainBattle(async method => {
    if (method === 'press') presses++;
    return { game_state: { ...state(), frame: frame++ } };
  }, { move_slot: 0 });
  assert.equal(result.training.reason, 'tap_unverified');
  assert.equal(presses, 1);
});

test('move learning or evolution party changes stop before input', async () => {
  let calls = 0;
  const result = await trainBattle(async () => {
    const current = state();
    if (calls++ > 0) current.party.members[0].moves = [{ id: 2 }];
    return { game_state: current };
  }, { move_slot: 0 });
  assert.equal(result.training.reason, 'party_or_moves_changed');
  assert.equal(calls, 2);
});

test('a new move-learning message stops animation waits immediately', async () => {
  let calls = 0;
  const result = await trainBattle(async () => {
    const current = state();
    if (calls++ > 0) current.recent_events = [{ type: 'message', text: 'Pokemon is trying to learn TACKLE!' }];
    return { game_state: current };
  }, { move_slot: 0 });
  assert.equal(result.training.reason, 'training_exception');
  assert.equal(calls, 2);
});
