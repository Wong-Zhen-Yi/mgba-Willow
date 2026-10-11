import test from 'node:test';
import assert from 'node:assert/strict';
import { handoff } from '../handoff.mjs';

test('handoff retains historical save location separately from current location', () => {
  const save = { type: 'save_confirmed', frame: 20, map_id: 3 };
  const state = { supported: true, available: true, frame: 30, map: { id: 4 }, player: { x: 7, y: 4 },
    interaction: 'overworld', story_progress: { badge_count: 3 }, recent_events: [save], party: {}, inventory: {} };
  const before = structuredClone(state);
  const result = handoff(state);
  assert.equal(result.location.map.id, 4);
  assert.deepEqual(result.last_observed_save, save);
  assert.equal(result.story_progress.badge_count, 3);
  assert.deepEqual(state, before);
});
test('handoff does not invent save confirmation or supported state', () => {
  assert.equal(handoff({ supported: false }).available, false);
  assert.equal(handoff({ supported: true }).last_observed_save, null);
  assert.match(handoff({ supported: true }).save_evidence, /does not mean/);
});
