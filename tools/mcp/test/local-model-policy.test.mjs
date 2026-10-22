import test from 'node:test';
import assert from 'node:assert/strict';
import { probabilityPolicy, stopExplanation } from '../../local-model/decision-policy.mjs';

const move = { id: 'move_5_4', method: 'move_to', args: { x: 5, y: 4 } };
const confirm = { id: 'press_A', method: 'press', args: { button: 'A' } };

test('choice thresholds distinguish bounded navigation from committing interactions', () => {
  assert.deepEqual(probabilityPolicy(move), { category: 'navigation', min_probability: 0.3 });
  assert.deepEqual(probabilityPolicy(confirm), { category: 'confirmation', min_probability: 0.6 });
  assert.deepEqual(probabilityPolicy({ id: 'stop' }), { category: 'model_stop', min_probability: 0 });
  const menu = { id: 'press_Down', method: 'press', args: {
    button: 'Down', expected_menu: { kind: 'battle_move', cursor: 0 },
  } };
  assert.equal(probabilityPolicy(menu).min_probability, 0.3);
  assert.equal(probabilityPolicy({ ...menu, args: { button: 'Down' } }).min_probability, 0.6,
    'A raw directional press is not classified as decoded menu navigation');
  assert.equal(probabilityPolicy({ id: 'wait', method: 'act', args: { buttons: [], frames: 8 } }).min_probability, 0.3);
  assert.equal(probabilityPolicy({ id: 'act', method: 'act', args: { buttons: ['A'], frames: 2 } }).min_probability, 0.6);
});

test('full distributions use a choice-count-aware navigation floor, never for confirmations or explicit overrides', () => {
  assert.equal(probabilityPolicy(move, { candidateCount: 6 }).min_probability, 1 / 6);
  assert.equal(probabilityPolicy(move, { candidateCount: 2 }).min_probability, 0.3);
  assert.equal(probabilityPolicy(confirm, { candidateCount: 6 }).min_probability, 0.6);
  assert.equal(probabilityPolicy(move, { candidateCount: 6, minProbability: 0.6 }).min_probability, 0.6);
  assert.equal(probabilityPolicy(move, { candidateCount: 6, minNavigationProbability: 0.4 }).min_probability, 0.4);
});

test('global threshold overrides remain strict unless a navigation override is explicitly supplied', () => {
  assert.equal(probabilityPolicy(move, { minProbability: 0.8 }).min_probability, 0.8);
  assert.equal(probabilityPolicy(confirm, { minProbability: 0.8 }).min_probability, 0.8);
  assert.equal(probabilityPolicy(move, { minProbability: 0.8, minNavigationProbability: 0.4 }).min_probability, 0.4);
  assert.equal(probabilityPolicy(confirm, { minProbability: 0.8, minNavigationProbability: 0.4 }).min_probability, 0.8);
});

test('stop messages explain rejected scores and real safety stops without claiming success', () => {
  const explanation = stopExplanation({ reason: 'low_probability', details: {
    action: 'press_A', probability: 0.38, min_probability: 0.6, category: 'confirmation',
  } });
  assert.match(explanation, /press_A.*38\.0%.*60\.0% confirmation threshold/);
  assert.match(explanation, /No input was sent/);
  assert.match(explanation, /not a calibrated success rate/);
  assert.match(stopExplanation({ reason: 'needs_human_inspection', details: {
    interaction: 'dialogue', menu: 'none',
  } }), /dialogue.*Handle the prompt manually/);
  assert.match(stopExplanation({ reason: 'model_stop' }), /does not establish goal completion/);
  assert.match(stopExplanation({ reason: 'human_interruption' }), /resume manually/);
  assert.match(stopExplanation({ reason: 'unsupported_or_unavailable_state' }), /No blind inputs/);
  assert.match(stopExplanation({ reason: 'unverified_confirmation' }), /confirmation was not repeated/);
});
