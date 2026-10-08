// Run only with a disposable test ROM open: this advances its game state.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { Bridge, descriptors } from '../bridge.mjs';

const client = new Client({ name: 'mgba-live-smoke', version: '1.0' });
await client.connect(new StdioClientTransport({ command: process.execPath, args: ['server.mjs'], stderr: 'inherit' }));
let connected;
const call = async (name, args = {}) => {
  const result = await client.callTool({ name, arguments: args });
  assert.ok(!result.isError, JSON.stringify(result.content));
  return result;
};
try {
  const list = await call('list_sessions');
  const sessions = list.structuredContent.sessions.filter(s => s.started && (!process.env.SMOKE_SESSION_ID || s.session_id === process.env.SMOKE_SESSION_ID));
  assert.equal(sessions.length, 1, 'Open exactly one disposable test ROM');
  const session = sessions[0];
  connected = await call('connect', { session_id: session.session_id });
  assert.equal(connected.structuredContent.paused, false);
  assert.equal(connected.content[1].mimeType, 'image/png');
  assert.equal(connected.structuredContent.mcp_enabled, true);
  assert.equal(connected.structuredContent.ai_speed, 4);
  assert.equal(connected.structuredContent.human_keys, 0);
  const start = connected.structuredContent.frame;
  await new Promise(resolve => setTimeout(resolve, 150));
  assert.ok((await call('observe')).structuredContent.frame > start, 'Game stopped while thinking');
  for (const frames of [1, 2, 8, 60, 600]) {
    const moved = await call('act', { buttons: ['A', 'Up'], frames });
    assert.equal(moved.structuredContent.action_end_frame - moved.structuredContent.action_start_frame, frames);
    assert.ok(moved.structuredContent.frame >= moved.structuredContent.action_end_frame);
    assert.equal(moved.structuredContent.paused, false);
    assert.equal(moved.structuredContent.ai_keys, 0, 'AI buttons stuck after action');
    assert.equal(moved.structuredContent.cancelled, false);
  }
  const idle = await call('act', { buttons: [], frames: 2 });
  assert.equal(idle.structuredContent.action_end_frame - idle.structuredContent.action_start_frame, 2);
  assert.equal(idle.structuredContent.paused, false);
  const imageFree = await call('act', { buttons: ['B'], frames: 2, screenshot: false });
  assert.equal(imageFree.content.length, 1);
  const batch = await call('act_sequence', { actions: [{ buttons: ['Right'], frames: 3 }, { buttons: ['A'], frames: 1 }, { buttons: [], frames: 2 }] });
  assert.equal(batch.structuredContent.steps.length, 3);
  assert.equal(batch.structuredContent.cancelled, false);
  let boundary = batch.structuredContent.action_start_frame;
  for (const [index, frames] of [3, 1, 2].entries()) {
    const step = batch.structuredContent.steps[index];
    assert.equal(step.action_start_frame, boundary);
    boundary += frames;
    assert.equal(step.action_end_frame, boundary);
  }
  assert.equal(boundary, batch.structuredContent.action_end_frame);
  const durations = {};
  for (const multiplier of [1, 4]) {
    assert.equal((await call('set_speed', { multiplier })).structuredContent.ai_speed, multiplier);
    const before = performance.now();
    await call('act', { buttons: [], frames: 60, screenshot: false });
    durations[multiplier] = performance.now() - before;
  }
  assert.ok(durations[4] < durations[1] * 0.75, `4x did not accelerate emulation: ${JSON.stringify(durations)}`);
  const individualStart = performance.now();
  for (let i = 0; i < 8; ++i) await call('act', { buttons: [], frames: 1, screenshot: false });
  const individualMs = performance.now() - individualStart;
  const batchStart = performance.now();
  await call('act_sequence', { actions: Array.from({ length: 8 }, () => ({ buttons: [], frames: 1 })), screenshot: false });
  const batchMs = performance.now() - batchStart;
  console.log(`Timing: 60 frames 1x=${durations[1].toFixed(1)}ms, 4x=${durations[4].toFixed(1)}ms; eight moves individual=${individualMs.toFixed(1)}ms, batch=${batchMs.toFixed(1)}ms (8 calls -> 1).`);
  const map = await call('memory_map');
  const rom = map.structuredContent.regions.find(region => region.name === 'cart0' && region.readable);
  assert.ok(rom);
  const memory = await call('read_memory', { address: rom.start, length: 32 });
  if (process.env.SMOKE_ROM) {
    const file = await readFile(process.env.SMOKE_ROM);
    assert.equal(memory.structuredContent.hex, file.subarray(0, 32).toString('hex'));
  }
  for (const args of [{ address: session.platform === 'GBA' ? 0x04000000 : 0xFF00, length: 1 }, { address: rom.end - 1, length: 2 }, { address: rom.start, length: 1, segment: 65535 }]) {
    assert.equal((await client.callTool({ name: 'read_memory', arguments: args })).isError, true);
  }
  const saved = await call('save_checkpoint', { name: 'mcp-smoke-test' });
  const before = await call('read_memory', { address: rom.start, length: 32 });
  const ram = map.structuredContent.regions.find(region => region.name === 'wram' && region.readable);
  const ramBefore = await call('read_memory', { address: ram.start, length: 4096 });
  await call('act', { buttons: ['Start'], frames: 15 });
  const restored = await call('load_checkpoint', { name: 'mcp-smoke-test' });
  assert.equal(restored.structuredContent.paused, false);
  assert.equal(restored.content[1].mimeType, 'image/png');
  assert.deepEqual((await call('read_memory', { address: rom.start, length: 32 })).structuredContent, before.structuredContent);
  assert.equal((await call('read_memory', { address: ram.start, length: 4096 })).structuredContent.hex.length, ramBefore.structuredContent.hex.length);
  // A second client must be rejected while the first owns the window.
  const descriptor = (await descriptors()).find(d => d.session_id === session.session_id);
  const competitor = await Bridge.open(descriptor);
  await assert.rejects(competitor.request('connect'), /another client/);
  // Validate bridge-side bounds independently of MCP schema validation.
  await assert.rejects(competitor.request('observe'), /connect first/);
  competitor.close();
  const validator = await Bridge.open(descriptor);
  await call('disconnect');
  await validator.request('connect');
  for (const args of [{ buttons: [], frames: 0 }, { buttons: [], frames: 601 }, { buttons: ['X'], frames: 1 }]) {
    await assert.rejects(validator.request('act', args), /Expected|Unknown/);
  }
  await assert.rejects(validator.request('save_checkpoint', { name: '../escape' }), /Checkpoint names/);
  await validator.request('disconnect');
  validator.close();
  await call('connect', { session_id: session.session_id });
  await call('disconnect');
  // Abrupt socket close releases AI input while ordinary gameplay continues.
  const abrupt = await Bridge.open(descriptor);
  await abrupt.request('connect');
  const action = abrupt.request('act', { buttons: ['B'], frames: 600 }).catch(() => {});
  await new Promise(resolve => setTimeout(resolve, 80));
  abrupt.close();
  await action;
  await new Promise(resolve => setTimeout(resolve, 100));
  const recovered = await call('connect', { session_id: session.session_id });
  assert.equal(recovered.structuredContent.paused, false);
  assert.equal(recovered.structuredContent.keys, 0);
  const ended = await call('disconnect');
  assert.equal(ended.structuredContent.paused, false);
  const status = await Bridge.open(descriptor);
  const released = await status.request('session');
  assert.equal(released.ai_control, false);
  assert.equal(released.ai_keys, 0);
  await new Promise(resolve => setTimeout(resolve, 150));
  const later = await status.request('session');
  assert.ok(later.frame > released.frame, 'Disconnect paused ordinary gameplay');
  assert.ok(later.frame - released.frame < 24, 'Ordinary speed was not restored');
  status.close();
  console.log(`PASS: ${session.platform} live MCP images, continuous play while thinking, exact button durations, input release, ROM reads, rejected ranges, checkpoint restore, exclusive ownership, reconnect, and abrupt disconnect.`);
} finally {
  if (connected) await client.callTool({ name: 'disconnect', arguments: {} }).catch(() => {});
  await client.close();
}
