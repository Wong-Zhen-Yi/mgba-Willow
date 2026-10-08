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
  const sessions = list.structuredContent.sessions.filter(s => s.started);
  assert.equal(sessions.length, 1, 'Open exactly one disposable test ROM');
  const session = sessions[0];
  connected = await call('connect', { session_id: session.session_id });
  assert.equal(connected.structuredContent.paused, true);
  assert.equal(connected.content[1].mimeType, 'image/png');
  const start = connected.structuredContent.frame;
  await new Promise(resolve => setTimeout(resolve, 150));
  assert.equal((await call('observe')).structuredContent.frame, start, 'Game advanced while thinking');
  let previous = start;
  for (const frames of [1, 2, 8, 60, 600]) {
    const moved = await call('act', { buttons: ['A', 'Up'], frames });
    assert.equal(moved.structuredContent.frame, previous + frames);
    assert.equal(moved.structuredContent.paused, true);
    assert.equal(moved.structuredContent.keys, 0, 'AI buttons stuck after action');
    previous += frames;
  }
  const idle = await call('act', { buttons: [], frames: 2 });
  assert.equal(idle.structuredContent.frame, previous + 2);
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
  assert.equal(restored.content[1].data, saved.content[1].data, 'Checkpoint screenshot did not restore');
  assert.deepEqual((await call('read_memory', { address: rom.start, length: 32 })).structuredContent, before.structuredContent);
  assert.deepEqual((await call('read_memory', { address: ram.start, length: 4096 })).structuredContent, ramBefore.structuredContent, 'Checkpoint RAM did not restore');
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
  // Abrupt socket close releases ownership and does not keep advancing frames.
  const abrupt = await Bridge.open(descriptor);
  const abruptStart = await abrupt.request('connect');
  const action = abrupt.request('act', { buttons: ['B'], frames: 600 }).catch(() => {});
  await new Promise(resolve => setTimeout(resolve, 80));
  abrupt.close();
  await action;
  await new Promise(resolve => setTimeout(resolve, 100));
  const recovered = await call('connect', { session_id: session.session_id });
  assert.equal(recovered.structuredContent.paused, true);
  assert.equal(recovered.structuredContent.keys, 0);
  assert.ok(recovered.structuredContent.frame < abruptStart.frame + 600, 'Disconnect failed to cancel an in-progress action');
  await call('disconnect');
  console.log(`PASS: ${session.platform} live MCP images, paused observations, exact steps, input release, ROM reads, rejected ranges, checkpoint restore, exclusive ownership, reconnect, and abrupt disconnect.`);
} finally {
  if (connected) await client.callTool({ name: 'disconnect', arguments: {} }).catch(() => {});
  await client.close();
}
