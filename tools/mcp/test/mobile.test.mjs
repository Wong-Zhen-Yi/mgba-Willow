import test from 'node:test';
import assert from 'node:assert/strict';
import { randomUUID } from 'node:crypto';
import { once } from 'node:events';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import WebSocket from 'ws';
import { createMobileServer, isTailscaleIPv4 } from '../mobile-server.mjs';

const ip = '100.94.12.10';
const origin = `http://${ip}:9444`;
const token = 'test-pairing-token';
const fixtureFrame = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

test('the mobile link accepts only Tailscale IPv4 addresses', () => {
  for (const valid of [ip, '100.64.0.1', '100.127.255.254']) assert.equal(isTailscaleIPv4(valid), true);
  for (const invalid of ['127.0.0.1', '100.128.0.1', '100.63.0.1', 'willow.ts.net', 'fd7a:115c:a1e0::1']) {
    assert.equal(isTailscaleIPv4(invalid), false);
  }
});

test('a paired mobile controller streams frames and controls input, speed and Luna Auto Play', { timeout: 20000 }, async t => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'willow-mobile-'));
  const id = randomUUID();
  const gameId = randomUUID();
  const name = `mgba-willow-ai-${id}`;
  const pipe = process.platform === 'win32' ? `\\\\.\\pipe\\${name}` : path.join(os.tmpdir(), name);
  const sockets = new Set();
  const game = { session_id: id, game_id: gameId, platform: 'GBA', started: true, paused: false, title: 'Pokémon Emerald' };
  const models = [{ id: 'terra', label: 'GPT-6.1 Terra (medium)' }, { id: 'luna', label: 'GPT-6 Luna (extra high)' }];
  const autoplayCalls = [];
  const speedCalls = [];
  let autoplay = { running: false, stopping: false, message: '' };
  let observeKeys;
  let release;
  let mobile;
  const fake = net.createServer(socket => {
    sockets.add(socket);
    let input = '';
    let attached = false;
    socket.setEncoding('utf8').on('data', chunk => {
      const lines = (input + chunk).split('\n');
      input = lines.pop();
      for (const line of lines) {
        const request = JSON.parse(line);
        let result = {};
        let error;
        switch (request.method) {
          case 'mobile_session': result = game; break;
          case 'mobile_connect':
            attached = request.args.game_id === gameId;
            if (attached) result = { ...game, autoplay, autoplay_models: models, speed: 0 };
            else error = 'Game changed';
            break;
          case 'mobile_frame':
            result = { frame: 1, paused: game.paused, autoplay };
            if (request.args.after !== 1) result.png = fixtureFrame.toString('base64');
            break;
          case 'mobile_autoplay':
            assert.ok(attached);
            autoplayCalls.push(request.args);
            autoplay = request.args.enabled ?
              { running: true, stopping: false, message: 'Auto Play is running with GPT-6 Luna · extra high.' } :
              { running: true, stopping: true, message: 'Auto Play is stopping…' };
            result = autoplay;
            break;
          case 'mobile_speed':
            assert.ok(attached);
            speedCalls.push(request.args.multiplier);
            result = { speed: request.args.multiplier };
            break;
          case 'mobile_keys': observeKeys?.(request.args.keys); break;
          default: assert.fail(`Unexpected method ${request.method}`);
        }
        socket.write(JSON.stringify({ id: request.id, result, error }) + '\n');
      }
    });
    socket.on('close', () => {
      sockets.delete(socket);
      if (attached) release?.();
    });
  });
  t.after(async () => {
    mobile?.close();
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => fake.close(resolve));
    await rm(root, { recursive: true, force: true });
  });
  fake.listen(pipe);
  await once(fake, 'listening');
  await writeFile(path.join(root, id + '.json'), JSON.stringify({ session_id: id, pipe: name, protocol: 1, pid: 1234 }));
  mobile = await createMobileServer({ origin, token, pid: 1234, directory: root });
  mobile.server.listen(0, '127.0.0.1');
  await once(mobile.server, 'listening');
  const endpoint = `127.0.0.1:${mobile.server.address().port}`;
  const options = { headers: { Host: new URL(origin).host } };
  const get = async (route, requestHeaders = options.headers) => {
    const reply = await new Promise((resolve, reject) => {
      http.get(`http://${endpoint}${route}`, { headers: requestHeaders }, resolve).once('error', reject);
    });
    let body = '';
    for await (const chunk of reply.setEncoding('utf8')) body += chunk;
    return { status: reply.statusCode, csp: reply.headers['content-security-policy'], body };
  };
  const page = await get('/');
  assert.equal(page.status, 200);
  assert.match(page.csp, /connect-src ws:\/\/100\.94\.12\.10:9444/);
  assert.match(page.body, /<button id="autoplay" type="button" disabled>Auto Play<\/button>/);
  assert.match(page.body, /<section id="settings-page" class="settings-page" aria-labelledby="settings-title" hidden>/);
  assert.equal(page.body.match(/<button class="open-settings" type="button">Settings<\/button>/g)?.length, 2);
  assert.doesNotMatch(page.body, /settings-dialog/);
  for (const control of ['speed', 'autoplay-model', 'video-mode', 'screen-filter', 'control-size', 'keep-awake', 'forget']) {
    assert.match(page.body, new RegExp(`id="${control}"`));
  }
  // Every phone setting, Auto Play's model included, lives on the one Settings page.
  assert.match(page.body, /<section aria-labelledby="autoplay-settings">[\s\S]*<select id="autoplay-model" disabled>/);
  assert.doesNotMatch(page.body, /<dialog/);
  assert.match(page.body, /<label for="speed">Game speed<\/label>/);
  assert.equal((await get('/', { Host: 'example.com' })).status, 404);
  assert.equal((await get('/Willow-Mobile.mobileconfig')).status, 404);

  const unauthorized = new WebSocket(`ws://${endpoint}/control`, { ...options, origin });
  await once(unauthorized, 'open');
  unauthorized.send(JSON.stringify({ type: 'pair', token: 'wrong' }));
  assert.equal((await once(unauthorized, 'close'))[0], 4001);

  const crossOrigin = new WebSocket(`ws://${endpoint}/control`, { ...options, origin: 'https://example.com' });
  await assert.rejects(once(crossOrigin, 'open'));
  const ws = new WebSocket(`ws://${endpoint}/control`, { ...options, origin });
  await once(ws, 'open');
  const send = message => ws.send(JSON.stringify(message));
  const read = type => new Promise(resolve => {
    ws.on('message', function onMessage(data, binary) {
      if (binary) return;
      const packet = JSON.parse(data);
      if (type && packet.type !== type) return;
      ws.off('message', onMessage);
      resolve(packet);
    });
  });
  send({ type: 'pair', token });
  assert.equal((await read()).type, 'paired');
  send({ type: 'autoplay', enabled: true });
  send({ type: 'speed', multiplier: 2 });
  send({ type: 'sessions' });
  assert.equal((await read()).games[0].title, game.title);
  send({ type: 'connect', session: id, game: randomUUID() });
  assert.equal((await read()).type, 'error');
  send({ type: 'connect', session: id, game: gameId });
  const packets = [];
  ws.on('message', (body, binary) => packets.push(binary ? body : JSON.parse(body)));
  const attached = await read();
  assert.equal(attached.type, 'attached');
  assert.deepEqual(attached.autoplay, autoplay);
  assert.deepEqual(attached.autoplay_models, models);
  assert.equal(attached.title, 'Pokémon Emerald');
  assert.equal(attached.speed, 0);
  if (!packets[1]) await once(ws, 'message');
  // PNG frames carry a leading 0 type byte.
  assert.deepEqual(packets[1], Buffer.concat([Buffer.from([0]), fixtureFrame]));
  const keys = new Promise(resolve => { observeKeys = resolve; });
  send({ type: 'keys', keys: 65 });
  assert.equal(await keys, 65);

  send({ type: 'speed', multiplier: '4' });
  send({ type: 'speed', multiplier: 3 });
  send({ type: 'speed', multiplier: 4 });
  assert.deepEqual(await read('speed'), { type: 'speed', speed: 4 });
  assert.deepEqual(speedCalls, [4]);

  send({ type: 'autoplay', enabled: 'true' });
  send({ type: 'autoplay', enabled: true, model: 3 });
  assert.equal((await read('autoplay-error')).message, 'Invalid Auto Play model.');
  send({ type: 'autoplay', enabled: true, model: 'luna' });
  assert.deepEqual(await read(), { type: 'autoplay', acknowledged: true, autoplay });
  assert.deepEqual(autoplayCalls, [{ enabled: true, model: 'luna' }]);

  // A pause and progress from the PC are sent even though the frame is unchanged.
  game.paused = true;
  autoplay = { ...autoplay, message: 'Auto Play is waiting. Resume the game to continue.' };
  const pause = read('pause');
  const progress = read('autoplay');
  assert.deepEqual(await pause, { type: 'pause', paused: true });
  assert.deepEqual(await progress, { type: 'autoplay', acknowledged: false, autoplay });

  // Speed can be selected while paused and can return to desktop control.
  send({ type: 'speed', multiplier: -1 });
  assert.deepEqual(await read('speed'), { type: 'speed', speed: -1 });
  send({ type: 'speed', multiplier: 0 });
  assert.deepEqual(await read('speed'), { type: 'speed', speed: 0 });
  assert.deepEqual(speedCalls, [4, -1, 0]);

  send({ type: 'autoplay', enabled: false });
  assert.deepEqual(await read(), { type: 'autoplay', acknowledged: true, autoplay });
  assert.equal(autoplay.stopping, true);
  assert.deepEqual(autoplayCalls, [{ enabled: true, model: 'luna' }, { enabled: false }]);
  autoplay = { running: false, stopping: false, message: 'Auto Play is off.' };
  assert.deepEqual(await read(), { type: 'autoplay', acknowledged: false, autoplay });

  const released = new Promise(resolve => { release = resolve; });
  ws.close();
  await released;
});

test('Tailscale Serve HTTPS origins allow only the matching secure WebSocket', async t => {
  const secure = 'https://willow.tail1234.ts.net:9444';
  const mobile = await createMobileServer({ origin: secure, token, pid: 1234, directory: os.tmpdir() });
  t.after(() => mobile.close());
  mobile.server.listen(0, '127.0.0.1');
  await once(mobile.server, 'listening');
  const reply = await new Promise((resolve, reject) => {
    http.get(`http://127.0.0.1:${mobile.server.address().port}/`, { headers: { Host: new URL(secure).host } }, resolve)
      .once('error', reject);
  });
  reply.resume();
  assert.equal(reply.statusCode, 200);
  assert.match(reply.headers['content-security-policy'], /connect-src wss:\/\/willow\.tail1234\.ts\.net:9444;/);
});

test('idle unpaired sockets cannot block a phone from pairing', { timeout: 10000 }, async t => {
  const mobile = await createMobileServer({ origin, token, pid: 1234, directory: os.tmpdir() });
  const idle = [];
  t.after(() => {
    for (const ws of idle) ws.terminate();
    mobile.close();
  });
  mobile.server.listen(0, '127.0.0.1');
  await once(mobile.server, 'listening');
  const url = `ws://127.0.0.1:${mobile.server.address().port}/control`;
  const open = async () => {
    const ws = new WebSocket(url, { headers: { Host: new URL(origin).host }, origin });
    await once(ws, 'open');
    return ws;
  };
  for (let i = 0; i < 12; i++) idle.push(await open());
  await new Promise(resolve => setTimeout(resolve, 100));
  assert.equal(idle.filter(ws => ws.readyState === WebSocket.OPEN).length, 8);
  assert.equal(idle[0].readyState, WebSocket.CLOSED);
  const phone = await open();
  idle.push(phone);
  phone.send(JSON.stringify({ type: 'pair', token }));
  assert.equal(JSON.parse((await once(phone, 'message'))[0]).type, 'paired');
});

test('the phone lists the PC library, loads artwork and launches a game', { timeout: 20000 }, async t => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'willow-library-'));
  const id = randomUUID();
  const name = `mgba-willow-ai-${id}`;
  const pipe = process.platform === 'win32' ? `\\\\.\\pipe\\${name}` : path.join(os.tmpdir(), name);
  const emerald = 'a'.repeat(20);
  const calls = [];
  let started = false;
  const fake = net.createServer(socket => {
    let input = '';
    socket.setEncoding('utf8').on('data', chunk => {
      const lines = (input + chunk).split('\n');
      input = lines.pop();
      for (const line of lines) {
        const request = JSON.parse(line);
        calls.push([request.method, request.args]);
        let result = {};
        let error;
        if (request.method === 'mobile_library') {
          result = { folder: true, scanning: false, current_id: started ? emerald : '',
            games: [{ id: emerald, title: 'Emerald', platform: 'GBA', art: true },
              { id: 'b'.repeat(20), title: 'Red', platform: 'GB', art: false }] };
        } else if (request.method === 'mobile_session') {
          result = { session_id: id, game_id: started ? 'game-1' : '', title: started ? 'Emerald' : '',
            platform: 'GBA', started };
        } else if (request.method === 'mobile_artwork') {
          result = { art: { [emerald]: Buffer.from('jpeg').toString('base64') } };
        } else if (request.method === 'mobile_launch') {
          if (request.args.id === emerald) { started = true; result = { launching: true }; }
          else error = 'That game is no longer in the library.';
        }
        socket.write(JSON.stringify({ id: request.id, result, error }) + '\n');
      }
    });
  });
  let mobile;
  let ws;
  t.after(async () => {
    ws?.terminate();
    mobile?.close();
    await new Promise(resolve => fake.close(resolve));
    await rm(root, { recursive: true, force: true });
  });
  fake.listen(pipe);
  await once(fake, 'listening');
  await writeFile(path.join(root, id + '.json'), JSON.stringify({ session_id: id, pipe: name, protocol: 1, pid: 1234 }));
  mobile = await createMobileServer({ origin, token, pid: 1234, directory: root });
  mobile.server.listen(0, '127.0.0.1');
  await once(mobile.server, 'listening');
  ws = new WebSocket(`ws://127.0.0.1:${mobile.server.address().port}/control`,
    { headers: { Host: new URL(origin).host }, origin });
  await once(ws, 'open');
  const send = message => ws.send(JSON.stringify(message));
  const read = async () => JSON.parse((await once(ws, 'message'))[0]);
  send({ type: 'pair', token });
  assert.equal((await read()).type, 'paired');

  send({ type: 'library' });
  const listing = await read();
  assert.equal(listing.type, 'library');
  assert.equal(listing.session_id, id);
  assert.deepEqual(listing.games.map(game => game.title), ['Emerald', 'Red']);
  assert.equal(listing.running, null);

  send({ type: 'artwork', session: id, ids: [emerald, '../../etc'] });
  send({ type: 'artwork', session: id, ids: [emerald] });
  const artwork = await read();
  assert.equal(artwork.type, 'artwork');
  assert.equal(Buffer.from(artwork.art[emerald], 'base64').toString(), 'jpeg');
  assert.equal(calls.filter(([method]) => method === 'mobile_artwork').length, 1);

  send({ type: 'launch', session: id, id: 'b'.repeat(20) });
  assert.deepEqual(await read(), { type: 'error', message: 'That game is no longer in the library.' });
  send({ type: 'launch', session: id, id: emerald });
  assert.deepEqual(await read(), { type: 'launching', id: emerald });
  send({ type: 'library' });
  const after = await read();
  assert.equal(after.current_id, emerald);
  assert.deepEqual(after.running, { game_id: 'game-1', title: 'Emerald', platform: 'GBA' });
});

test('a phone can switch from NVENC HEVC to H.264 and PNG', { timeout: 10000 }, async t => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'willow-mobile-'));
  const id = randomUUID();
  const gameId = randomUUID();
  const name = `mgba-willow-ai-${randomUUID()}`;
  const pipe = process.platform === 'win32' ? `\\\\.\\pipe\\${name}` : path.join(os.tmpdir(), name);
  const game = { session_id: id, game_id: gameId, platform: 'GBA', started: true, paused: false, title: 'Test ROM' };
  const requests = [];
  const sockets = new Set();
  let frame = 0;
  let failing = false;
  const fake = net.createServer(socket => {
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
    let input = '';
    socket.setEncoding('utf8').on('data', chunk => {
      const lines = (input + chunk).split('\n');
      input = lines.pop();
      for (const line of lines) {
        const request = JSON.parse(line);
        let result = {};
        if (request.method === 'mobile_session') result = game;
        else if (request.method === 'mobile_connect') result = { ...game, autoplay: null, speed: 0 };
        else if (request.method === 'mobile_frame') {
          requests.push(request.args);
          result = { frame: ++frame, paused: false, autoplay: null };
          if (request.args.codec === 'hevc') {
            result.video = { encoder: 'hevc_nvenc', hardware: true, codec: 'hvc1.1.6.L90.B0', width: 480, height: 320,
                             packets: [{ key: true, data: Buffer.from([0, 0, 0, 1, 38, 1]).toString('base64') }] };
          }
          else if (request.args.codec !== 'h264') result.png = fixtureFrame.toString('base64');
          else if (failing) result.video_error = 'libx264: not built in';
          else {
            const key = Boolean(request.args.keyframe);
            result.video = { encoder: 'h264_nvenc', hardware: true, codec: 'avc1.64001e', width: 480, height: 320,
                             packets: [{ key, data: Buffer.from([0, 0, 0, 1, key ? 0x65 : 0x41, frame]).toString('base64') }] };
          }
          // Throttle the fake so the back-to-back loop does not flood the test.
          setTimeout(() => socket.write(JSON.stringify({ id: request.id, result }) + '\n'), 5);
          continue;
        }
        socket.write(JSON.stringify({ id: request.id, result }) + '\n');
      }
    });
  });
  let mobile;
  let ws;
  t.after(async () => {
    ws?.terminate();
    mobile?.close();
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => fake.close(resolve));
    await rm(root, { recursive: true, force: true });
  });
  fake.listen(pipe);
  await once(fake, 'listening');
  await writeFile(path.join(root, id + '.json'), JSON.stringify({ session_id: id, pipe: name, protocol: 1, pid: 1234 }));
  mobile = await createMobileServer({ origin, token, pid: 1234, directory: root });
  mobile.server.listen(0, '127.0.0.1');
  await once(mobile.server, 'listening');
  ws = new WebSocket(`ws://127.0.0.1:${mobile.server.address().port}/control`,
    { headers: { Host: new URL(origin).host }, origin });
  await once(ws, 'open');
  const send = message => ws.send(JSON.stringify(message));
  const messages = [];
  let notify;
  ws.on('message', (body, binary) => {
    messages.push(binary ? body : JSON.parse(body));
    notify?.();
  });
  const next = async match => {
    while (true) {
      const index = messages.findIndex(match);
      if (index >= 0) return messages.splice(0, index + 1).pop();
      await new Promise(resolve => { notify = resolve; });
    }
  };
  const json = type => next(message => !Buffer.isBuffer(message) && message.type === type);
  const chunk = type => next(message => Buffer.isBuffer(message) && message[0] === type);

  send({ type: 'pair', token });
  await json('paired');
  send({ type: 'video', codec: 'hevc' });
  send({ type: 'video', codec: 'vp9' });
  send({ type: 'connect', session: id, game: gameId });
  await json('attached');
  assert.deepEqual(await json('video'),
    { type: 'video', codec: 'hevc', encoder: 'hevc_nvenc', hardware: true, format: 'hvc1.1.6.L90.B0', width: 480, height: 320 });
  assert.deepEqual([...(await chunk(2)).subarray(1)], [0, 0, 0, 1, 38, 1]);
  send({ type: 'video', codec: 'h264' });

  // The decoder configuration arrives before the first keyframe.
  assert.deepEqual(await json('video'),
    { type: 'video', codec: 'h264', encoder: 'h264_nvenc', hardware: true, format: 'avc1.64001e', width: 480, height: 320 });
  const key = await chunk(2);
  assert.deepEqual([...key.subarray(1, 6)], [0, 0, 0, 1, 0x65]);
  await chunk(1);
  await chunk(1);
  const video = requests.filter(args => args.codec === 'h264');
  assert.equal(video[0].keyframe, true);
  assert.equal(video[0].wait, true);
  assert.equal(video.at(-1).keyframe, false);
  assert.equal(video.at(-1).after, frame - 1);
  // Back-to-back pulls outpace the 50 ms PNG timer.
  assert.ok(video.length >= 3);
  assert.ok(!requests.some(args => args.codec === 'vp9'));

  send({ type: 'keyframe' });
  await chunk(2);

  // When the PC cannot encode, the phone is told to expect PNG and gets it.
  failing = true;
  assert.deepEqual(await json('video'), { type: 'video', codec: 'png', failed: 'h264', reason: 'libx264: not built in' });
  assert.deepEqual(await chunk(0), Buffer.concat([Buffer.from([0]), fixtureFrame]));
});
