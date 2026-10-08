import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { Bridge, descriptors, listSessions } from './bridge.mjs';

const instructions = `Control a visible mGBA game through short observe/action cycles. The emulator pauses between moves, so take time to inspect screenshots. Start with list_sessions and connect. Use 1–8 frames near hazards and longer moves only when safe; act with no buttons to wait. Save a checkpoint before risky moves. Use memory_map before reading memory: raw bytes are not labeled health, coordinates, or progress unless verified for this exact game. L/R exist only on GBA. Stop at the user's goal or when help is needed, then disconnect. Never reset or replace the user's game or assume that beating a game is guaranteed. GPT-6 Luna with medium reasoning is the recommended Codex chat setting; this MCP does not select or invoke a model.`;
const server = new McpServer({ name: 'mgba-willow', version: '1.0.0' }, { instructions });
let bridge;
let queue = Promise.resolve();

export function content(result) {
  if (result.error) throw new Error(result.error);
  const { png, ...metadata } = result;
  return {
    content: [
      { type: 'text', text: JSON.stringify(metadata) },
      ...(png ? [{ type: 'image', mimeType: 'image/png', data: png }] : []),
    ],
    structuredContent: metadata,
  };
}

function register(name, description, inputSchema, readOnly, callback) {
  server.registerTool(name, {
    description, inputSchema,
    annotations: { readOnlyHint: readOnly, destructiveHint: false, openWorldHint: false, idempotentHint: readOnly },
  }, args => {
    const run = queue.then(async () => {
      try { return content(await callback(args)); }
      catch (error) { return { content: [{ type: 'text', text: error.message }], isError: true }; }
    });
    queue = run.catch(() => {});
    return run;
  });
}

function call(method, args) {
  if (!bridge) throw new Error('No game connected. Use list_sessions, then connect.');
  return bridge.request(method, args);
}

register('list_sessions', 'List visible mGBA windows, game titles, platforms, and AI control status. Open a ROM with Launch mGBA.cmd first.', {}, true,
  async () => ({ sessions: await listSessions() }));
register('connect', 'Acquire AI control of one game window, pause it, and return its screenshot. Another controlling client is rejected.', {
  session_id: z.string().uuid(),
}, false, async ({ session_id }) => {
  if (bridge) {
    try { await bridge.request('disconnect'); } catch {}
    bridge.close(); bridge = undefined;
  }
  const descriptor = (await descriptors()).find(item => item.session_id === session_id);
  if (!descriptor) throw new Error('Session not found. Use list_sessions again.');
  const candidate = await Bridge.open(descriptor);
  try {
    const result = await candidate.request('connect');
    bridge = candidate;
    return result;
  } catch (error) { candidate.close(); throw error; }
});
register('observe', 'Get the current game screenshot, frame count, and pause status without advancing the game.', {}, true,
  args => call('observe', args));
register('act', 'Hold the requested buttons for exactly 1–600 frames, release them, pause, and return a screenshot. Empty buttons means wait. L/R require GBA.', {
  buttons: z.array(z.enum(['A', 'B', 'Start', 'Select', 'Up', 'Down', 'Left', 'Right', 'L', 'R'])).max(10),
  frames: z.number().int().min(1).max(600),
}, false, args => call('act', args));
register('memory_map', 'List emulator memory regions, supported raw read ranges, and bank limits. MMIO and virtual memory are unavailable.', {}, true,
  args => call('memory_map', args));
register('read_memory', 'Read 1–4096 raw bytes from a supported memory region as hexadecimal. Address is a numeric byte address; segment selects a bank, or -1 uses the current mapping.', {
  address: z.number().int().min(0).max(0xFFFFFFFF),
  length: z.number().int().min(1).max(4096),
  segment: z.number().int().min(-1).max(65535).optional(),
}, true, args => call('read_memory', args));
const checkpointSchema = { name: z.string().regex(/^[A-Za-z0-9_-]{1,64}$/) };
register('save_checkpoint', 'Save a named AI checkpoint for this ROM outside normal save-state slots. An existing checkpoint of the same name is replaced.', checkpointSchema, false,
  args => call('save_checkpoint', args));
register('load_checkpoint', 'Restore a named checkpoint for this ROM and return its screenshot. Restores game progress and saved data from that checkpoint.', checkpointSchema, false,
  args => call('load_checkpoint', args));
register('disconnect', 'Release all AI inputs and leave the game paused for user takeover.', {}, false, async args => {
  try { return await call('disconnect', args); }
  finally { bridge?.close(); bridge = undefined; }
});

async function shutdown() {
  bridge?.close();
  await server.close();
}
process.on('SIGINT', () => shutdown().finally(() => process.exit(0)));
process.on('SIGTERM', () => shutdown().finally(() => process.exit(0)));
await server.connect(new StdioServerTransport());
process.stdin.on('end', () => shutdown());
