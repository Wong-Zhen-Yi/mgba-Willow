import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { Bridge, descriptors, listSessions } from './bridge.mjs';
import { StateResponses } from './state-responses.mjs';
import { guardedMovement } from './visual-movement.mjs';
import { handoff } from './handoff.mjs';
import { guardedVerifiedInput } from './control-policy.mjs';

const instructions = `Control a visible mGBA game with shared human input. Multiple agents can connect to the same window; coordinate goals because they share game progress, speed, and checkpoints. Only one action runs at a time: if busy, observe and retry after completion. Observations and memory reads remain available during another agent's action. The bridge is enabled automatically while mGBA is open; the user can disable it in AI > MCP Enabled. Start with list_sessions and connect. Human directions override conflicting AI directions; other buttons combine. AI speed defaults to 4x and can be changed with set_speed or AI > AI Speed. The emulator runs while you think, so observations become stale quickly. Use short observe/action cycles near hazards. Use act_sequence for predictable movement to reduce model/tool round trips, with up to 200 steps and at most 600 total frames. Use screenshot:false only when a fresh image is unnecessary; obtain a new observation before uncertain decisions. Cancelled actions report cancelled:true and only completed sequence steps; do not assume planned moves completed. A manual pause must be resumed by the user. Disconnect cancels only your pending action; other connected agents retain control. The last agent disconnecting restores ordinary speed without pausing or clearing human buttons. Save a checkpoint before risky moves, using agent-specific names to avoid overwriting another agent's checkpoint. Use memory_map before reading memory: bytes are not labeled health or coordinates unless verified for this exact game. L/R exist only on GBA. Stop at the user's goal or when help is needed, then disconnect. Never reset or replace the user's game. This MCP does not select or invoke a model.`;
const verification = `For verified Emerald, use one press then inspect each new prompt, including switch and move-learning prompts. Confirmation batches are rejected. Raw travel checks state between input frames and stops at new decision evidence; prefer move_to for efficient collision-aware travel. Inspect live tiles, behavior, occupancy and reachable paths before selecting a destination; do not repeat no-path targets or assume Gym holes are active. move_to avoids potential trainer sightlines by default; avoid_trainers:false permits deliberate exposure. movement_available checks animation restrictions as well as overworld state; verify coordinate progress after geysers or forced movement. Battle, dialogue and menu responses always contain full state. For other deltas, replace changed top-level fields and delete removed_state_fields; verify base_frame and request full_state:true if it does not match. Inspect live battler HP, cursor, selected move and target; request a full screenshot observation when they seem inconsistent. Record milestones and evidence immediately in story notes and the objective tracker. During long sessions, make in-game saves at safe points and verify save_confirmed; checkpoints are separate. Obtain get_handoff before stopping.`;
const server = new McpServer({ name: 'mgba-willow', version: '1.1.0' }, { instructions: `${instructions} ${verification} Only atomic:true certifies screenshot/state frame alignment. Unsupported ROMs retain raw tools.` });
let bridge;
const stateResponses = new StateResponses();
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
      try { return content(stateResponses.apply(await callback(args), args.full_state === true)); }
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

async function controlledInput(method, args) {
  // Refresh known supported state: the game runs between MCP requests.
  if (stateResponses.previous?.supported === true) {
    const observation = await call('get_game_state', { screenshot: false });
    return guardedVerifiedInput(call, method, args, observation);
  }
  return method === 'press' ? call(method, args) : guardedMovement(call, method, args);
}

register('list_sessions', 'List visible mGBA windows, game titles, platforms, and AI control status. Open a ROM with Launch mGBA.cmd first.', {}, true,
  async () => ({ sessions: await listSessions() }));
register('connect', 'Join shared AI control of one game window, preserve its pause state, and return its screenshot. Multiple agents can connect; actions run one at a time. Human controls remain active.', {
  session_id: z.string().uuid(),
}, false, async ({ session_id }) => {
  stateResponses.reset();
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
const observationSchema = { screenshot: z.boolean().optional(), full_state: z.boolean().optional() };
register('observe', 'Get a screenshot and decoded state. atomic:true certifies a shared completed emulated frame; hardware rendering reports atomic:false. Unsupported ROMs retain screenshots and raw tools. State changes use a per-client baseline; full_state:true returns the complete state.', observationSchema, true,
  args => call('observe', args));
register('get_game_state', 'Read verified Emerald map coordinates, facing, interaction state, party HP and moves, bag inventory, and numeric event flags. No screenshot by default. Subsequent responses contain top-level state changes; full_state:true refreshes the baseline. Unsupported ROMs report supported:false.', observationSchema, true,
  args => call('get_game_state', args));
register('get_handoff', 'Read a fresh complete snapshot and return location, party, inventory, verified badge progress, bounded message evidence and the last observed save confirmation. Does not advance frames, save the game, or write notes. Save evidence is historical and may be unavailable after restart or restore.', {
  screenshot: z.boolean().optional(),
}, true, async args => {
  const result = await call('get_game_state', args);
  return { ...result, handoff: handoff(result.game_state) };
});
register('get_local_map', 'Read a radius of live Emerald collision tiles, elevations, behaviors, ladders, one-way ledges, active NPCs, warps and map connections. Coordinates exclude the game’s seven-tile border. Unknown tiles are never assumed passable.', {
  ...observationSchema, radius: z.number().int().min(1).max(16).optional(),
}, true, args => call('get_local_map', args));
register('move_to', 'Avoid potential trainer sightlines by default; avoid_trainers:false permits intentional exposure. Follow a bounded on-foot path on the current verified Emerald map. Check every emulated frame and release input on encounters, dialogue, scripted movement, transitions, human input, blockage or timeout. Returns progress.reason; completed:false requires replanning. Verified cardinal ledges and destination-only north-facing animated doors are supported; other special traversal is excluded. No screenshot by default.', {
  ...observationSchema, x: z.number().int().min(0).max(511), y: z.number().int().min(0).max(511),
  max_frames: z.number().int().min(1).max(600).optional(),
  avoid_trainers: z.boolean().optional(),
}, false, args => call('move_to', args));
register('wait_until', 'Advance with no AI buttons until a decoded Emerald condition holds for stable_frames (default 2), or max_frames (default 600) expires. Stops on human input or manual pause. map_transition_complete means the overworld is ready; it does not require a transition to begin.', {
  ...observationSchema,
  condition: z.enum(['battle_menu_ready', 'battle_move_ready', 'battle_target_ready', 'overworld_ready', 'map_transition_complete', 'dialogue']),
  max_frames: z.number().int().min(1).max(600).optional(), stable_frames: z.number().int().min(1).max(60).optional(),
}, false, args => call('wait_until', args));
register('press', 'Tap one button, then keep AI input released for an explicit interval. Defaults to a one-frame hold and two-frame release; works on supported and unsupported ROMs. No screenshot by default.', {
  ...observationSchema, button: z.enum(['A', 'B', 'Start', 'Select', 'Up', 'Down', 'Left', 'Right', 'L', 'R']),
  hold_frames: z.number().int().min(1).max(60).optional(), release_frames: z.number().int().min(1).max(60).optional(),
}, false, args => controlledInput('press', args));
const actionSchema = {
  buttons: z.array(z.enum(['A', 'B', 'Start', 'Select', 'Up', 'Down', 'Left', 'Right', 'L', 'R'])).max(10),
  frames: z.number().int().min(1).max(600),
};
register('act', 'Verified Emerald requires single confirmation taps and stops raw travel at new decision evidence or 60 unchanged input frames. Hold AI buttons for up to 1–600 frames, then release only AI input. Unsupported ROM directional holds stop early on 64 frames of stable screenshots; inspect progress.requires_replan and progress.reason before moving again. Visual stalls indicate possible obstacles, not verified collision. Returns actual frame boundaries and a screenshot by default. Manual pause cancels pending actions.', {
  ...actionSchema,
  ...observationSchema,
}, false, args => controlledInput('act', args));
register('act_sequence', 'Verified Emerald rejects confirmation batches and checks decoded state between input frames; prefer move_to for travel. Execute 1–200 predictable moves, at most 600 total frames. Unsupported ROM long directional holds use screenshot checks and stop the sequence on a visual stall; progress.requires_replan means inspect and choose another route. Return only fully completed step boundaries and one final screenshot by default. Pause or disconnect cancels remaining moves.', {
  actions: z.array(z.object(actionSchema)).min(1).max(200).refine(actions => actions.reduce((total, action) => total + action.frames, 0) <= 600, 'Sequence exceeds 600 total frames'),
  ...observationSchema,
}, false, args => controlledInput('act_sequence', args));
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
  args => { stateResponses.reset(); return call('load_checkpoint', args); });
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

