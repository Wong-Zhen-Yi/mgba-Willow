import test from 'node:test';
import assert from 'node:assert/strict';
import { deflateSync } from 'node:zlib';
import { guardedMovement } from '../visual-movement.mjs';

function png(value) {
  const chunk = (type, data) => {
    const b = Buffer.alloc(data.length + 12);
    b.writeUInt32BE(data.length); b.write(type, 4); data.copy(b, 8); return b;
  };
  return Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'),
    chunk('IHDR', Buffer.from([0, 0, 0, 1, 0, 0, 0, 1, 8, 2, 0, 0, 0])),
    chunk('IDAT', deflateSync(Buffer.from([0, value, value, value]))), chunk('IEND', Buffer.alloc(0))]).toString('base64');
}
function bridge({ moving = false, supported = false, cancel = false } = {}) {
  let frame = 0, count = 0;
  const calls = [];
  const request = async (method, args) => {
    calls.push({ method, args });
    const start = frame;
    if (method === 'act') { frame += args.frames; count++; }
    return { frame, action_start_frame: start, action_end_frame: frame,
      game_state: { supported }, png: png(moving ? count : 0), cancelled: cancel && method === 'act' };
  };
  return { request, calls };
}
test('static unsupported screen stops a long hold after 64 frames and skips subsequent steps', async () => {
  const b = bridge();
  const r = await guardedMovement(b.request, 'act_sequence', { actions: [
    { buttons: ['Right'], frames: 240 }, { buttons: ['A'], frames: 8 }], screenshot: false });
  assert.equal(r.action_end_frame, 64);
  assert.equal(r.progress.reason, 'visual_stall');
  assert.equal(r.progress.completed, false);
  assert.deepEqual(r.steps, []);
  assert.equal(r.png, undefined);
  assert.equal(b.calls.filter(c => c.method === 'act').length, 2);
});
test('changing screen completes bounded chunks with accurate frame boundaries', async () => {
  const b = bridge({ moving: true });
  const r = await guardedMovement(b.request, 'act', { buttons: ['Up'], frames: 90 });
  assert.equal(r.action_start_frame, 0); assert.equal(r.action_end_frame, 90);
  assert.equal(r.progress.completed, true);
  assert.deepEqual(b.calls.slice(1).map(c => c.args.frames), [32, 32, 26]);
});
test('verified ROMs and menu buttons preserve existing raw behavior', async () => {
  const b = bridge({ supported: true });
  await guardedMovement(b.request, 'act', { buttons: ['Up'], frames: 120 });
  assert.equal(b.calls[1].args.frames, 120);
  const menu = bridge();
  await guardedMovement(menu.request, 'act', { buttons: ['A'], frames: 120 });
  assert.equal(menu.calls.length, 1);
});
test('cancellation prevents subsequent chunks and sequence steps', async () => {
  const b = bridge({ cancel: true });
  const r = await guardedMovement(b.request, 'act_sequence', { actions: [
    { buttons: ['Down'], frames: 120 }, { buttons: ['A'], frames: 8 }] });
  assert.equal(r.cancelled, true); assert.equal(r.progress.completed, false);
  assert.deepEqual(r.steps, []); assert.equal(b.calls.length, 2);
});
