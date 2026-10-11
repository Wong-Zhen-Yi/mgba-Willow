import test from 'node:test';
import assert from 'node:assert/strict';
import { inputProblem, guardedVerifiedInput } from '../control-policy.mjs';

const state = { supported: true, available: true, interaction: 'overworld', movement_available: true,
  map: { id: 1 }, menu: { kind: 'none' }, player: { moving: false }, recent_events: [] };

test('decision batches are rejected before any frames advance', async () => {
  const actions = [{ buttons: ['A'], frames: 1 }, { buttons: [], frames: 2 }, { buttons: ['A'], frames: 1 }];
  let calls = 0;
  await assert.rejects(guardedVerifiedInput(async () => { calls++; }, 'act_sequence', { actions }, { game_state: state }), /single-button tap/);
  assert.equal(calls, 0);
  assert.match(inputProblem(state, 'press', { button: 'A', hold_frames: 60 }), /single-button tap/);
  assert.equal(inputProblem(state, 'press', { button: 'A' }), undefined);
  assert.equal(inputProblem({ supported: false }, 'act_sequence', { actions }), undefined);
});

test('menu directions are taps and restricted movement waits without input', () => {
  assert.match(inputProblem({ ...state, interaction: 'battle' }, 'act', { buttons: ['Down'], frames: 20 }), /single-button tap/);
  assert.match(inputProblem({ ...state, movement_available: false }, 'press', { button: 'Right' }), /restricted/);
  assert.equal(inputProblem({ ...state, movement_available: false }, 'act', { buttons: [], frames: 60 }), undefined);
});

test('travel stops on the first encounter frame and skips remaining steps', async () => {
  let frame = 10;
  const inputs = [];
  const request = async (method, args) => {
    inputs.push(args.buttons);
    const start = frame; frame += args.frames;
    return { action_start_frame: start, action_end_frame: frame,
      game_state: frame >= 12 ? { ...state, interaction: 'battle', menu: { kind: 'battle_action' } } : state };
  };
  const result = await guardedVerifiedInput(request, 'act_sequence', {
    actions: [{ buttons: ['Right'], frames: 20 }, { buttons: ['Down'], frames: 20 }], screenshot: false,
  }, { game_state: state });
  assert.deepEqual(inputs, [['Right'], ['Right']]);
  assert.equal(result.action_start_frame, 10);
  assert.equal(result.action_end_frame, 12);
  assert.equal(result.progress.reason, 'decision_point');
  assert.equal(result.progress.completed, false);
  assert.deepEqual(result.steps, []);
});

test('new dialogue pages, map transitions and human input require inspection', async () => {
  for (const change of [{ recent_events: [{ type: 'message', text: 'Switch Pokemon?' }] },
    { map: { id: 2 } }, { human_keys: 16 }, { cancelled: true }]) {
    let calls = 0;
    const result = await guardedVerifiedInput(async () => {
      calls++;
      const { human_keys, cancelled, ...fields } = change;
      return { game_state: { ...state, ...fields }, human_keys, cancelled,
        action_start_frame: 10, action_end_frame: 11 };
    }, 'act', { buttons: ['Right'], frames: 20, screenshot: false }, { game_state: state });
    assert.equal(calls, 1);
    assert.equal(result.progress.completed, false);
    assert.equal(result.progress.requires_inspection, true);
  }
});

test('successful travel encodes only the final observation and keeps action boundaries', async () => {
  const calls = [];
  let frame = 10;
  const result = await guardedVerifiedInput(async (method, args) => {
    calls.push({ method, args });
    if (method === 'observe') return { game_state: { ...state, frame: 15 }, frame: 15, png: 'image' };
    const start = frame; frame += args.frames;
    return { game_state: state, action_start_frame: start, action_end_frame: frame };
  }, 'act', { buttons: ['Right'], frames: 3 }, { game_state: state });
  assert.equal(calls.filter(call => call.method === 'act').length, 3);
  assert.ok(calls.slice(0, -1).every(call => call.args.screenshot === false));
  assert.equal(calls.at(-1).method, 'observe');
  assert.equal(result.action_start_frame, 10);
  assert.equal(result.action_end_frame, 13);
  assert.equal(result.frame, 15);
  assert.equal(result.png, 'image');
  assert.equal(result.progress.completed, true);
});

test('blocked raw travel stops instead of spending the full frame budget guessing', async () => {
  const blocked = { ...state, player: { x: 1, y: 2, moving: false } };
  let frame = 10;
  const result = await guardedVerifiedInput(async (method, args) => {
    const start = frame; frame += args.frames;
    return { game_state: blocked, action_start_frame: start, action_end_frame: frame };
  }, 'act', { buttons: ['Right'], frames: 600, screenshot: false }, { game_state: blocked });
  assert.equal(result.action_end_frame, 70);
  assert.equal(result.progress.reason, 'position_unchanged');
  assert.equal(result.progress.completed, false);
});
