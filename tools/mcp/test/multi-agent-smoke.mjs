// Run only against an isolated disposable ROM session; this advances the game.
import assert from 'node:assert/strict';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { Bridge, descriptors } from '../bridge.mjs';

assert.ok(process.env.SMOKE_SESSION_ID, 'Set SMOKE_SESSION_ID to a disposable test session');
const clients = [];
const bridges = [];
async function newClient(name) {
  const client = new Client({ name, version: '1.0' });
  clients.push(client);
  await client.connect(new StdioClientTransport({ command: process.execPath, args: ['server.mjs'], stderr: 'inherit' }));
  return client;
}
async function call(client, name, args = {}) {
  const result = await client.callTool({ name, arguments: args });
  assert.ok(!result.isError, JSON.stringify(result.content));
  return result.structuredContent;
}
async function openBridge(descriptor) {
  const bridge = await Bridge.open(descriptor);
  bridges.push(bridge);
  return bridge;
}
try {
  const a = await newClient('multi-agent-a');
  const b = await newClient('multi-agent-b');
  const session_id = process.env.SMOKE_SESSION_ID;
  const descriptor = (await descriptors()).find(d => d.session_id === session_id);
  assert.ok(descriptor);
  const status = await openBridge(descriptor);
  await assert.rejects(status.request('observe'), /connect first/);
  assert.equal((await call(a, 'connect', { session_id })).connected_clients, 1);
  assert.equal((await call(b, 'connect', { session_id })).connected_clients, 2);
  assert.equal((await call(b, 'connect', { session_id })).connected_clients, 2, 'Connect must be idempotent');
  assert.equal((await call(a, 'observe')).connected_clients, 2);
  assert.equal((await call(b, 'observe')).connected_clients, 2);
  await call(a, 'set_speed', { multiplier: 1 });
  const completedAction = call(a, 'act', { buttons: [], frames: 60, screenshot: false });
  assert.equal((await call(b, 'observe')).connected_clients, 2);
  const completed = await completedAction;
  assert.equal(completed.cancelled, false);
  assert.equal(completed.action_end_frame - completed.action_start_frame, 60);
  const action = a.callTool({ name: 'act', arguments: { buttons: ['B'], frames: 600, screenshot: false } }).then(result => ({ result }), error => ({ error }));
  const deadline = Date.now() + 4000;
  while (!(await status.request('session')).ai_keys) {
    assert.ok(Date.now() < deadline, 'Action never started');
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  const busy = await b.callTool({ name: 'act', arguments: { buttons: ['A'], frames: 1 } });
  assert.equal(busy.isError, true);
  assert.match(busy.content[0].text, /already in progress/);
  for (const [name, args] of [['set_speed', { multiplier: 8 }], ['save_checkpoint', { name: 'busy-must-not-save' }], ['load_checkpoint', { name: 'busy-must-not-load' }]]) {
    const rejected = await b.callTool({ name, arguments: args });
    assert.equal(rejected.isError, true);
    assert.match(rejected.content[0].text, /already in progress/);
  }
  const observed = await call(b, 'observe');
  assert.equal(observed.ai_speed, 1, 'Rejected speed change must not affect the action');
  assert.equal(observed.ai_keys, 2, 'Rejected action must not replace active B input');
  const map = await call(b, 'memory_map');
  const rom = map.regions.find(region => region.name === 'cart0' && region.readable);
  assert.ok(rom);
  assert.equal((await call(b, 'read_memory', { address: rom.start, length: 1 })).hex.length, 2);
  const c = await openBridge(descriptor);
  assert.equal((await c.request('connect')).connected_clients, 3, 'Connect while another agent acts');
  await call(b, 'disconnect');
  assert.equal((await c.request('observe')).connected_clients, 2);
  assert.equal((await status.request('session')).ai_keys, 2, 'Spectator disconnect must not cancel another action');
  await a.close();
  await action;
  const remaining = await c.request('observe');
  assert.equal(remaining.connected_clients, 1);
  assert.equal(remaining.ai_keys, 0, 'Abrupt action-owner disconnect must release its input');
  assert.equal(remaining.ai_control, true, 'Other agent must retain AI control');
  const moved = await c.request('act', { buttons: ['A'], frames: 2, screenshot: false });
  assert.equal(moved.cancelled, false);
  assert.equal(moved.action_end_frame - moved.action_start_frame, 2);
  const cancelledAction = c.request('act', { buttons: ['B'], frames: 600, screenshot: false });
  await c.request('disconnect');
  const cancelled = await cancelledAction;
  assert.equal(cancelled.cancelled, true);
  assert.ok(!Object.hasOwn(cancelled, 'action_end_frame'));
  await assert.rejects(c.request('observe'), /connect first/);
  const released = await status.request('session');
  assert.equal(released.connected_clients, 0);
  assert.equal(released.ai_control, false);
  assert.equal(released.ai_keys, 0);
  assert.equal(released.paused, false);
  await call(b, 'connect', { session_id });
  const sequence = await call(b, 'act_sequence', { actions: [{ buttons: [], frames: 2 }, { buttons: [], frames: 3 }], screenshot: false });
  assert.equal(sequence.cancelled, false);
  assert.equal(sequence.steps.length, 2);
  assert.equal(sequence.action_end_frame - sequence.action_start_frame, 5);
  await call(b, 'set_speed', { multiplier: 4 });
  await call(b, 'disconnect');
  console.log('PASS: two MCP agents, idempotent shared connect, busy action rejection, observation and memory reads during actions, third-client connect during action, independent disconnect, abrupt owner cancellation, survivor actions, explicit owner cancellation, last-client cleanup, and reconnect sequences');
} finally {
  for (const bridge of bridges) bridge.close();
  for (const client of clients) await client.close().catch(() => {});
}
