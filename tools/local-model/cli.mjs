import { parseArgs } from 'node:util';
import { mkdir, appendFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { Bridge, descriptors, listSessions } from '../mcp/bridge.mjs';
import { KevClient } from './kev-client.mjs';
import { LunaClient } from './luna-client.mjs';
import { play } from './player.mjs';
import { stopExplanation } from './decision-policy.mjs';

const { values } = parseArgs({ options: {
  goal: { type: 'string' }, session: { type: 'string' }, list: { type: 'boolean' },
  'game-id': { type: 'string' },
  url: { type: 'string', default: process.env.KEV_BASE_URL || 'http://127.0.0.1:8008' },
  steps: { type: 'string', default: '10000' }, minutes: { type: 'string', default: '720' },
  'luna-help': { type: 'boolean', default: process.env.LUNA_HELP === '1' },
  'max-assists': { type: 'string', default: '20' },
  'min-probability': { type: 'string' }, 'min-navigation-probability': { type: 'string' },
  'log-dir': { type: 'string' },
  help: { type: 'boolean' },
} });
if (values.help) {
  console.log('Local Kev-4B player (decoded Emerald only)\n\nnode tools/local-model/cli.mjs --goal "Explore nearby tiles" [--session UUID]\n  --list                    List loaded games without taking control\n  --url ORIGIN              Local Kev server (default http://127.0.0.1:8008)\n  --steps N                 Maximum iterations (default 10000)\n  --minutes N               Wall-clock budget (default 720)\n  --min-probability P       Override the threshold for every action\n  --min-navigation-probability P  Navigation override (default min(0.30, 1/choices))\n  --luna-help               Enable paid GPT-6 Luna recovery (LUNA_API_KEY, LUNA_MODEL)\n  --max-assists N           Remote call cap per run (default 20; 0 disables)\n  --log-dir PATH            Full state/action/evidence JSONL and latest handoff\n\nCtrl+C disconnects this player. The game keeps running; pause/save manually.');
} else {
  let bridge;
  const controller = new AbortController();
  const cancel = () => { controller.abort(); bridge?.close(); };
  process.once('SIGINT', cancel);
  process.once('SIGTERM', cancel);
  let log;
  try {
    if (!values.list && (!values.goal?.trim() || values.goal.length > 512)) throw new Error('Supply --goal with 1–512 characters. Use --help for usage.');
    const sessions = (await listSessions()).filter(session => session.started);
    if (values.list) console.log(JSON.stringify(sessions, null, 2));
    else {
      const model = new KevClient({ baseUrl: values.url, apiKey: process.env.KEV_API_KEY });
      const luna = values['luna-help'] ? new LunaClient({ baseUrl: process.env.LUNA_BASE_URL,
        apiKey: process.env.LUNA_API_KEY, model: process.env.LUNA_MODEL }) : undefined;
      const selected = values.session ? sessions.find(session => session.session_id === values.session)
        : sessions.length === 1 ? sessions[0] : undefined;
      if (!selected) throw new Error('Open a ROM in Launch mGBA.cmd. If more than one game is open, use --list then --session UUID.');
      if (values['game-id'] && selected.game_id !== values['game-id']) throw new Error('The selected game changed. Enable the local model again.');
      if (selected.connected_clients) throw new Error('Disconnect other agents before starting the local player.');
      const directory = values['log-dir'] || path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), '.local', 'share'), 'mgba-willow', 'ai', 'local-model');
      await mkdir(directory, { recursive: true });
      const logPath = path.join(directory, `${new Date().toISOString().replace(/[:.]/g, '-')}-${process.pid}.jsonl`);
      log = async record => {
        if (record.type === 'handoff') record = { ...record, explanation: stopExplanation(record) };
        await appendFile(logPath, JSON.stringify({ at: new Date().toISOString(), ...record }) + '\n');
        if (record.type === 'handoff') {
          await writeFile(path.join(directory, 'latest-handoff.json'), JSON.stringify(record, null, 2) + '\n');
        }
        if (record.type === 'assistance_requested') console.log(`Asking GPT-6 Luna for help: ${record.reason} (call ${record.attempt}/${values['max-assists']}).`);
        if (record.type === 'assistance') console.log(`Luna: ${record.action}. ${record.guidance ?? ''}`);
        if (record.type === 'assistance_error') console.error(`Luna help unavailable: ${record.message}`);
        if (record.type === 'model_retry') console.log(`Retrying Kev inference (${record.attempt}/2): ${record.message}`);
        if (record.type === 'action' && record.source === 'route_recovery') console.log(`Breaking a navigation loop: ${record.action}`);
        if (record.type === 'decision') {
          console.log(`Step ${record.step}: ${record.choice.id} (p=${record.choice.probability.toFixed(3)}, ${record.policy.category} minimum=${record.policy.min_probability.toFixed(2)}, ${record.policy.accepted ? 'accepted' : 'rejected'})`);
        }
      };
      const descriptor = (await descriptors()).find(item => item.session_id === selected.session_id);
      if (!descriptor) throw new Error('Session closed. Open the game and retry.');
      bridge = await Bridge.open(descriptor);
      if (controller.signal.aborted) throw new Error('Cancelled');
      await bridge.request('connect');
      const request = async (method, args) => {
        const result = await bridge.request(method, args);
        const gameScoped = Object.hasOwn(result, 'game_id') ||
          ['session', 'observe', 'get_game_state', 'get_local_map', 'act', 'press', 'move_to', 'wait_until'].includes(method);
        if (values['game-id'] && gameScoped && result.game_id !== values['game-id']) {
          throw new Error('The selected game changed; local-player input stopped.');
        }
        return result;
      };
      if (values['game-id']) await request('session');
      console.log(`Playing ${selected.title} using local Kev. Ctrl+C stops AI input.\nLog: ${logPath}`);
      console.log(luna ? `Luna recovery enabled: ${process.env.LUNA_MODEL}; at most ${values['max-assists']} paid remote calls. Decoded game state is shared when stuck.`
        : 'Luna recovery off. Set LUNA_HELP=1, LUNA_API_KEY and LUNA_MODEL before launching to enable it.');
      const result = await play({ request, model, luna, maxAssists: Number(values['max-assists']),
        goal: values.goal, signal: controller.signal, log, maxSteps: Number(values.steps),
        maxMs: Number(values.minutes) * 60000,
        minProbability: values['min-probability'] === undefined ? undefined : Number(values['min-probability']),
        minNavigationProbability: values['min-navigation-probability'] === undefined ? undefined : Number(values['min-navigation-probability']) });
      console.log(`Stopped: ${result.reason}.\n${stopExplanation(result)}\nInspect the game and make an in-game save at a safe point; goal completion is not guaranteed.`);
    }
  } catch (error) {
    console.error(`Local player: ${error.message}`);
    if (log) await log({ type: 'error', message: error.message }).catch(() => {});
    process.exitCode = 1;
  } finally {
    try { if (bridge && !bridge.socket.destroyed) await bridge.request('disconnect'); } catch {}
    bridge?.close();
    process.removeListener('SIGINT', cancel);
    process.removeListener('SIGTERM', cancel);
  }
}
