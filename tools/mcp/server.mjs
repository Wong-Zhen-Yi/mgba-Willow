import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { Bridge, descriptors, listSessions } from './bridge.mjs';

const instructions = `Control a visible mGBA game with shared human input. Multiple agents can connect to the same window; coordinate goals because they share game progress, speed, and checkpoints. Only one action runs at a time: if busy, observe and retry after completion. Observations and memory reads remain available during another agent's action. The bridge is enabled automatically while mGBA is open; the user can disable it in AI > MCP Enabled. Start with list_sessions and connect. Human directions override conflicting AI directions; other buttons combine. AI speed defaults to 4x and can be changed with set_speed or AI > AI Speed. The emulator runs while you think, so observations become stale quickly. Use short observe/action cycles near hazards. Use act_sequence for predictable movement to reduce model/tool round trips, with up to 200 steps and at most 600 total frames. Use screenshot:false only when a fresh image is unnecessary; obtain a new observation before uncertain decisions. Cancelled actions report cancelled:true and only completed sequence steps; do not assume planned moves completed. A manual pause must be resumed by the user. Disconnect cancels only your pending action; other connected agents retain control. The last agent disconnecting restores ordinary speed without pausing or clearing human buttons. Save a checkpoint before risky moves, using agent-specific names to avoid overwriting another agent's checkpoint. Use memory_map before reading memory: bytes are not labeled health or coordinates unless verified for this exact game. L/R exist only on GBA. Stop at the user's goal or when help is needed, then disconnect. Never reset or replace the user's game. This MCP does not select or invoke a model.`;
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
register('connect', 'Join shared AI control of one game window, preserve its pause state, and return its screenshot. Multiple agents can connect; actions run one at a time. Human controls remain active.', {
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
register('observe', 'Get a snapshot of the live game, frame count, and pause status. The game continues running after the snapshot.', {}, true,
  args => call('observe', args));
const actionSchema = {
  buttons: z.array(z.enum(['A', 'B', 'Start', 'Select', 'Up', 'Down', 'Left', 'Right', 'L', 'R'])).max(10),
  frames: z.number().int().min(1).max(600),
};
register('act', 'Hold AI buttons for exactly 1–600 frames, then release only AI input. Human controls remain active. Returns frame boundaries and a screenshot by default; screenshot:false skips image encoding. Manual pause cancels pending actions.', {
  ...actionSchema,
  screenshot: z.boolean().optional(),
}, false, args => call('act', args));
register('act_sequence', 'Execute 1–200 predictable moves without intermediate tool round trips, at most 600 total frames. Return each completed step’s frame boundaries and one final screenshot by default. Pause or disconnect cancels remaining moves.', {
  actions: z.array(z.object(actionSchema)).min(1).max(200).refine(actions => actions.reduce((total, action) => total + action.frames, 0) <= 600, 'Sequence exceeds 600 total frames'),
  screenshot: z.boolean().optional(),
}, false, args => call('act_sequence', args));
register('set_speed', 'Set the persisted AI gameplay speed, shared with AI > AI Speed. Human fast-forward controls temporarily take priority. Ordinary speed settings are restored when AI control ends.', {
  multiplier: z.union([z.literal(1), z.literal(2), z.literal(4), z.literal(8), z.literal('maximum')]),
}, false, args => call('set_speed', args));
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
register('disconnect', 'Disconnect this agent and cancel only its pending action. Other agents retain control; the last disconnect restores ordinary speed. Preserve human buttons and pause state.', {}, false, async args => {
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
