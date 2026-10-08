import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { Bridge, listSessions } from '../bridge.mjs';

async function fixture(t, handler) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'mgba-mcp-test-'));
  const directory = path.join(root, 'mgba-willow', 'ai', 'sessions');
  await mkdir(directory, { recursive: true });
  const id = randomUUID();
  const descriptor = { session_id: id, pipe: `mgba-willow-ai-${id}`, protocol: 1 };
  const endpoint = process.platform === 'win32' ? `\\\\.\\pipe\\${descriptor.pipe}` : path.join(os.tmpdir(), descriptor.pipe);
  const sockets = new Set();
  const server = net.createServer(socket => {
    sockets.add(socket);
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
    let buffer = '';
    socket.on('data', chunk => {
      buffer += chunk;
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const req = JSON.parse(buffer.slice(0, end));
        buffer = buffer.slice(end + 1);
        handler(socket, req, id);
      }
    });
  });
  await new Promise(resolve => server.listen(endpoint, resolve));
  await writeFile(path.join(directory, id + '.json'), JSON.stringify(descriptor));
  t.after(async () => {
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => server.close(resolve));
    await rm(root, { recursive: true, force: true });
  });
  return { root, directory, id, descriptor };
}

test('bridge correlates fragmented responses and propagates errors', async t => {
  const f = await fixture(t, (socket, req) => {
    const message = JSON.stringify(req.method === 'fail' ? { id: req.id, error: 'Rejected' } : { id: req.id, result: { method: req.method } }) + '\n';
    socket.write(message.slice(0, 5));
    setImmediate(() => socket.write(message.slice(5)));
  });
  const bridge = await Bridge.open(f.descriptor);
  t.after(() => bridge.close());
  assert.deepEqual(await bridge.request('observe'), { method: 'observe' });
  await assert.rejects(bridge.request('fail'), /Rejected/);
  assert.deepEqual(await bridge.request('observe'), { method: 'observe' });
});

test('timeouts close the connection and reject pending calls', async t => {
  const f = await fixture(t, () => {});
  const bridge = await Bridge.open(f.descriptor);
  bridge.timeout = 30;
  await assert.rejects(bridge.request('act'), /timed out/);
  assert.equal(bridge.socket.destroyed, true);
});

test('malformed responses reject requests rather than crashing', async t => {
  const f = await fixture(t, socket => socket.write('invalid-json\n'));
  const bridge = await Bridge.open(f.descriptor);
  await assert.rejects(bridge.request('observe'), /Malformed/);
});

test('session discovery ignores stale and invalid descriptors', async t => {
  const f = await fixture(t, (socket, req, id) => socket.write(JSON.stringify({ id: req.id, result: { session_id: id, started: true } }) + '\n'));
  const staleId = randomUUID();
  await writeFile(path.join(f.directory, staleId + '.json'), JSON.stringify({ session_id: staleId, pipe: `mgba-willow-ai-${staleId}`, protocol: 1 }));
  await writeFile(path.join(f.directory, randomUUID() + '.json'), 'bad');
  assert.deepEqual(await listSessions(f.directory), [{ session_id: f.id, started: true }]);
});

test('MCP handshake, tools, schema validation, images, and reconnect', async t => {
  let frame = 10;
  const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==';
  const calls = [];
  const f = await fixture(t, (socket, req, id) => {
    calls.push(req);
    if (req.method === 'act') frame += req.args.frames;
    const result = req.method === 'session' ? { session_id: id, started: true, title: 'Test' }
      : req.method === 'disconnect' ? { paused: true }
      : req.method === 'read_memory' ? { hex: '0001' }
      : { png, frame, paused: true };
    socket.write(JSON.stringify({ id: req.id, result }) + '\n');
  });
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [path.resolve('server.mjs')],
    env: { ...process.env, LOCALAPPDATA: f.root },
    stderr: 'pipe',
  });
  let errors = '';
  transport.stderr?.on('data', chunk => { errors += chunk; });
  const client = new Client({ name: 'mgba-tests', version: '1.0' });
  t.after(() => client.close());
  await client.connect(transport);
  const { tools } = await client.listTools();
  assert.deepEqual(tools.map(tool => tool.name).sort(), ['act', 'connect', 'disconnect', 'list_sessions', 'load_checkpoint', 'memory_map', 'observe', 'read_memory', 'save_checkpoint'].sort());
  const call = (name, args = {}) => client.callTool({ name, arguments: args });
  assert.equal((await call('observe')).isError, true);
  assert.equal((await call('list_sessions')).structuredContent.sessions[0].session_id, f.id);
  const connected = await call('connect', { session_id: f.id });
  assert.equal(connected.content[1].type, 'image');
  assert.equal(connected.content[1].mimeType, 'image/png');
  assert.equal(connected.structuredContent.png, undefined);
  const result = await call('act', { buttons: ['A', 'Up'], frames: 3 });
  assert.equal(result.structuredContent.frame, 13);
  for (const args of [{ buttons: ['X'], frames: 1 }, { buttons: [], frames: 0 }, { buttons: [], frames: 601 }, { buttons: [], frames: 1.5 }]) {
    assert.equal((await call('act', args)).isError, true);
  }
  assert.equal(calls.filter(call => call.method === 'act').length, 1);
  assert.equal((await call('read_memory', { address: -1, length: 1 })).isError, true);
  assert.equal((await call('read_memory', { address: 0, length: 4097 })).isError, true);
  assert.equal((await call('save_checkpoint', { name: '../escape' })).isError, true);
  await call('disconnect');
  assert.equal((await call('observe')).isError, true);
  assert.equal((await call('connect', { session_id: f.id })).isError, undefined);
  await call('disconnect');
  assert.equal(errors, '');
});
