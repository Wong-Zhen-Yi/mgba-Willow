import net from 'node:net';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

export const sessionDirectory = path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), '.local', 'share'), 'mgba-willow', 'ai', 'sessions');

export class Bridge {
  constructor(socket, timeout = 35000) {
    this.socket = socket;
    this.timeout = timeout;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = '';
    socket.setEncoding('utf8');
    socket.on('data', chunk => {
      this.buffer += chunk;
      if (this.buffer.length > 16 * 1024 * 1024) return socket.destroy(new Error('Bridge response exceeds size limit'));
      let end;
      while ((end = this.buffer.indexOf('\n')) >= 0) {
        const line = this.buffer.slice(0, end);
        this.buffer = this.buffer.slice(end + 1);
        try {
          const response = JSON.parse(line);
          const request = this.pending.get(response.id);
          if (!request) continue;
          clearTimeout(request.timer);
          this.pending.delete(response.id);
          if (response.error) request.reject(new Error(response.error));
          else request.resolve(response.result);
        } catch { socket.destroy(new Error('Malformed bridge response')); }
      }
    });
    socket.on('error', error => this.fail(error));
    socket.on('close', () => this.fail(new Error('mGBA disconnected. Reopen the game and connect again.')));
  }

  fail(error) {
    for (const request of this.pending.values()) {
      clearTimeout(request.timer);
      request.reject(error);
    }
    this.pending.clear();
  }

  request(method, args = {}) {
    if (this.socket.destroyed) return Promise.reject(new Error('No mGBA connection. Connect again.'));
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error('mGBA request timed out; the connection was closed to release controls.'));
        this.close();
      }, this.timeout);
      this.pending.set(id, { resolve, reject, timer });
      this.socket.write(JSON.stringify({ id, method, args }) + '\n');
    });
  }

  close() { this.socket.destroy(); }

  static async open(descriptor, timeout = 1500) {
    if (descriptor.protocol !== 1 || !/^mgba-willow-ai-[0-9a-f-]{36}$/.test(descriptor.pipe)) {
      throw new Error('Unsupported mGBA bridge descriptor');
    }
    const pipe = process.platform === 'win32' ? `\\\\.\\pipe\\${descriptor.pipe}` : path.join(os.tmpdir(), descriptor.pipe);
    const socket = net.createConnection(pipe);
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => { socket.destroy(); reject(new Error('mGBA is not responding')); }, timeout);
      socket.once('connect', () => { clearTimeout(timer); resolve(); });
      socket.once('error', error => { clearTimeout(timer); reject(error); });
    });
    return new Bridge(socket);
  }
}

export async function descriptors(directory = sessionDirectory) {
  let files;
  try { files = await readdir(directory); }
  catch (error) { if (error.code === 'ENOENT') return []; throw error; }
  const results = await Promise.all(files.filter(name => /^[0-9a-f-]{36}\.json$/.test(name)).map(async name => {
    try {
      const descriptor = JSON.parse(await readFile(path.join(directory, name), 'utf8'));
      if (descriptor.session_id + '.json' !== name) return null;
      return descriptor;
    } catch { return null; }
  }));
  return results.filter(Boolean);
}

export async function listSessions(directory = sessionDirectory) {
  const sessions = await Promise.all((await descriptors(directory)).map(async descriptor => {
    let bridge;
    try {
      bridge = await Bridge.open(descriptor);
      return await bridge.request('session');
    } catch { return null; } // Ignore stale descriptors from crashed emulator processes.
    finally { bridge?.close(); }
  }));
  return sessions.filter(Boolean);
}
