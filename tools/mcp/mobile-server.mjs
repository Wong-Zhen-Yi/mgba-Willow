import http from 'node:http';
import { timingSafeEqual } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { isIPv4 } from 'node:net';
import { WebSocket, WebSocketServer } from 'ws';
import { Bridge, descriptors } from './bridge.mjs';

const files = new Map([
  ['/', ['mobile.html', 'text/html; charset=utf-8']],
  ['/mobile.css', ['mobile.css', 'text/css; charset=utf-8']],
  ['/mobile.js', ['mobile.js', 'text/javascript; charset=utf-8']],
]);

const pageHeaders = {
  'Cache-Control': 'no-store',
  'Content-Security-Policy': "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' blob:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'",
  'Referrer-Policy': 'no-referrer',
  'X-Content-Type-Options': 'nosniff',
};

export function isTailscaleIPv4(ip) {
  return isIPv4(ip) && ip.startsWith('100.') && (+ip.split('.')[1] & 0xc0) === 64;
}

// Loopback HTTP behind Tailscale Funnel/Serve, which terminates HTTPS (or forwards raw TCP for the numeric-IP fallback).
export async function createMobileServer({ origin, token, pid, directory }) {
  const host = new URL(origin).host;
  const proof = Buffer.from(token);
  const headers = { ...pageHeaders, 'Content-Security-Policy': pageHeaders['Content-Security-Policy']
    .replace("connect-src 'self'", `connect-src ${origin.replace(/^http/, 'ws')}`) };
  const assets = new Map(await Promise.all([...files].map(async ([route, [name, type]]) =>
    [route, { type, body: await readFile(new URL(`./mobile/${name}`, import.meta.url)) }])));
  const server = http.createServer((request, response) => {
    const asset = request.headers.host === host && request.method === 'GET' && assets.get(request.url);
    if (!asset) return response.writeHead(404, headers).end();
    response.writeHead(200, { ...headers, 'Content-Type': asset.type }).end(asset.body);
  });
  const sockets = new WebSocketServer({ noServer: true, maxPayload: 1024 });
  let controller;
  // Funnel makes the page public: evict the oldest unpaired socket so strangers cannot fill every slot.
  const unpaired = new Set();
  const candidates = async () => (await descriptors(directory)).filter(descriptor => descriptor.pid === pid);
  const gameId = /^[0-9a-f]{20}$/;
  // One short-lived bridge request against a session; null when that window does not answer.
  const ask = async (descriptor, work, timeout = 3000) => {
    let client;
    try {
      client = await Bridge.open(descriptor);
      client.timeout = timeout;
      return await work(client);
    } catch { return null; }
    finally { client?.close(); }
  };

  server.on('upgrade', (request, socket, head) => {
    if (request.url !== '/control' || request.headers.host !== host || request.headers.origin !== origin) {
      socket.destroy();
      return;
    }
    sockets.handleUpgrade(request, socket, head, ws => sockets.emit('connection', ws));
  });

  sockets.on('connection', ws => {
    let bridge;
    let attaching = false;
    let scanning = false;
    let browsing = false;
    let fetchingArt = false;
    let launching = false;
    let capturing = false;
    let autoplayRequest;
    let pendingInputs = 0;
    let platform;
    let lastFrame;
    let lastPaused;
    let lastAutoPlay;
    let alive = true;

    const send = message => {
      if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(message));
    };
    function updateAutoPlay(autoplay, acknowledged = false) {
      const key = JSON.stringify(autoplay);
      if (acknowledged || key !== lastAutoPlay) send({ type: 'autoplay', autoplay, acknowledged });
      lastAutoPlay = key;
    }
    function lost(error, client) {
      if (client !== bridge) return;
      bridge.close();
      bridge = undefined;
      send({ type: 'detached', message: error.message });
    }

    const authenticate = setTimeout(() => ws.terminate(), 5000);
    unpaired.add(ws);
    if (unpaired.size > 8) {
      const oldest = unpaired.values().next().value;
      unpaired.delete(oldest);
      oldest.terminate();
    }
    const heartbeat = setInterval(() => {
      if (!alive) return ws.terminate();
      alive = false;
      ws.ping();
    }, 5000);
    const frames = setInterval(async () => {
      if (!bridge || capturing || ws.bufferedAmount > 128 * 1024) return;
      const client = bridge;
      capturing = true;
      try {
        const result = await client.request('mobile_frame', { after: lastFrame });
        if (client !== bridge || ws.readyState !== WebSocket.OPEN) return;
        if (lastPaused !== result.paused) {
          lastPaused = result.paused;
          send({ type: 'pause', paused: lastPaused });
        }
        updateAutoPlay(result.autoplay);
        if (result.png) {
          lastFrame = result.frame;
          ws.send(Buffer.from(result.png, 'base64'));
        }
      } catch (error) {
        lost(error, client);
      } finally {
        capturing = false;
      }
    }, 50);

    ws.on('pong', () => { alive = true; });
    ws.on('error', () => ws.terminate());
    ws.on('close', () => {
      clearTimeout(authenticate);
      unpaired.delete(ws);
      clearInterval(heartbeat);
      clearInterval(frames);
      bridge?.close();
      bridge = undefined;
      if (controller === ws) controller = undefined;
    });
    ws.on('message', async (body, binary) => {
      if (binary) return ws.close(1003);
      let message;
      try { message = JSON.parse(body); } catch { return ws.close(1007); }
      if (!message || typeof message !== 'object') return ws.close(1007);
      if (controller !== ws) {
        const supplied = Buffer.from(typeof message.token === 'string' ? message.token : '');
        if (message.type !== 'pair' || supplied.length !== proof.length || !timingSafeEqual(supplied, proof)) {
          return ws.close(4001, 'Scan the current pairing code');
        }
        if (controller) return ws.close(4002, 'Another controller is connected');
        clearTimeout(authenticate);
        unpaired.delete(ws);
        controller = ws;
        send({ type: 'paired' });
        return;
      }

      if (message.type === 'sessions' && !scanning && !bridge) {
        scanning = true;
        try {
          const sessions = await Promise.all((await candidates()).map(async descriptor => {
            let client;
            try {
              client = await Bridge.open(descriptor);
              client.timeout = 1500;
              return await client.request('mobile_session');
            } catch { return null; }
            finally { client?.close(); }
          }));
          send({ type: 'sessions', games: sessions.filter(game => game?.started && game.game_id)
            .map(({ session_id, game_id, title, platform }) => ({ session_id, game_id, title, platform })) });
        } catch {
          send({ type: 'error', message: 'Could not find the running mGBA window.' });
        } finally {
          scanning = false;
        }
        return;
      }
      if (message.type === 'library' && !browsing && !bridge) {
        browsing = true;
        try {
          let best;
          for (const descriptor of await candidates()) {
            const result = await ask(descriptor, async client =>
              ({ library: await client.request('mobile_library'), session: await client.request('mobile_session') }));
            if (result && (!best || result.library.games.length > best.library.games.length)) best = result;
          }
          if (!best) throw new Error('Could not reach Willow on the PC.');
          const { library, session } = best;
          send({
            type: 'library',
            session_id: session.session_id,
            folder: library.folder,
            scanning: library.scanning,
            current_id: library.current_id,
            games: library.games.map(({ id, title, platform, art }) => ({ id, title, platform, art })),
            running: session.started && session.game_id ?
              { game_id: session.game_id, title: session.title, platform: session.platform } : null,
          });
        } catch {
          send({ type: 'error', message: 'Could not reach Willow on the PC. Keep AI → MCP Enabled checked.' });
        } finally {
          browsing = false;
        }
        return;
      }
      if (message.type === 'artwork' && !fetchingArt) {
        const ids = message.ids;
        if (!Array.isArray(ids) || ids.length > 24 || !ids.every(id => typeof id === 'string' && gameId.test(id))) return;
        fetchingArt = true;
        try {
          const descriptor = (await candidates()).find(item => item.session_id === message.session);
          const result = descriptor && await ask(descriptor, client => client.request('mobile_artwork', { ids }), 8000);
          send({ type: 'artwork', ids, art: result?.art ?? {} });
        } finally {
          fetchingArt = false;
        }
        return;
      }
      if (message.type === 'launch' && !launching && !bridge) {
        if (typeof message.id !== 'string' || !gameId.test(message.id)) return;
        launching = true;
        try {
          const descriptor = (await candidates()).find(item => item.session_id === message.session);
          if (!descriptor) throw new Error('Willow closed on the PC. Reload this page.');
          const client = await Bridge.open(descriptor);
          client.timeout = 5000;
          try { await client.request('mobile_launch', { id: message.id }); }
          finally { client.close(); }
          send({ type: 'launching', id: message.id });
        } catch (error) {
          send({ type: 'error', message: error.message });
        } finally {
          launching = false;
        }
        return;
      }
      if (message.type === 'connect' && !attaching && !bridge) {
        attaching = true;
        let client;
        try {
          const descriptor = (await candidates()).find(item => item.session_id === message.session);
          if (!descriptor || typeof message.game !== 'string') throw new Error('Select a running game.');
          client = await Bridge.open(descriptor);
          client.timeout = 3000;
          const game = await client.request('mobile_connect', { game_id: message.game });
          if (controller !== ws || ws.readyState !== WebSocket.OPEN) {
            client.close();
            return;
          }
          bridge = client;
          platform = game.platform;
          lastFrame = undefined;
          lastPaused = game.paused;
          lastAutoPlay = JSON.stringify(game.autoplay);
          send({ type: 'attached', title: game.title, platform, paused: lastPaused, autoplay: game.autoplay });
        } catch (error) {
          client?.close();
          send({ type: 'error', message: error.message });
        } finally {
          attaching = false;
        }
        return;
      }
      if (message.type === 'disconnect') {
        bridge?.close();
        bridge = undefined;
        send({ type: 'detached' });
        return;
      }
      if (message.type === 'autoplay' && bridge) {
        if (typeof message.enabled !== 'boolean' || autoplayRequest === bridge) return;
        const client = bridge;
        autoplayRequest = client;
        try {
          const result = await client.request('mobile_autoplay', { enabled: message.enabled });
          if (client === bridge) updateAutoPlay(result, true);
        } catch (error) {
          if (client === bridge) send({ type: 'autoplay-error', message: error.message });
        } finally {
          if (autoplayRequest === client) autoplayRequest = undefined;
        }
        return;
      }
      if (message.type === 'keys' && bridge) {
        const keys = message.keys;
        if (!Number.isInteger(keys) || keys < 0 || keys > (platform === 'GBA' ? 1023 : 255)) return;
        if (pendingInputs >= 32) return ws.terminate();
        const client = bridge;
        pendingInputs++;
        try { await client.request('mobile_keys', { keys }); }
        catch (error) { lost(error, client); }
        finally { pendingInputs--; }
      }
    });
  });

  server.on('close', () => sockets.close());
  return {
    server,
    close() {
      for (const ws of sockets.clients) ws.terminate();
      server.close();
      server.closeAllConnections();
    },
  };
}
