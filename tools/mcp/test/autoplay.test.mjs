import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { autoPlayPrompt, codexArguments, describe, profiles, runAutoPlay, turnStatus } from '../autoplay.mjs';

const sessionId = '0f3c3a52-8a6e-4d8e-9a43-0f1b2c3d4e5f';

// A stand-in for codex.exe: replays one scripted turn per call and logs its arguments.
const fakeCodex = `
const fs = require('node:fs');
const log = process.env.FAKE_CODEX_LOG;
const calls = fs.existsSync(log) ? JSON.parse(fs.readFileSync(log, 'utf8')) : [];
calls.push(process.argv.slice(2));
fs.writeFileSync(log, JSON.stringify(calls));
const turn = JSON.parse(fs.readFileSync(process.env.FAKE_CODEX_PLAN, 'utf8'))[calls.length - 1];
if (turn.fail) { process.stderr.write(turn.fail + '\\n'); process.exit(1); }
const emit = event => process.stdout.write(JSON.stringify(event) + '\\n');
emit({ type: 'thread.started', thread_id: 'thread-1' });
if (turn.hang) { setInterval(() => {}, 1000); return; }
for (const tool of turn.actions ?? Array(turn.tools ?? 0).fill({ tool: 'press' })) {
  const item = { type: 'mcp_tool_call', server: 'mgba-willow', status: 'completed', ...tool };
  emit({ type: 'item.started', item });
  emit({ type: 'item.completed', item });
}
emit({ type: 'item.completed', item: { type: 'agent_message', text: turn.message } });
emit({ type: 'turn.completed' });
`;

async function harness(t, plan) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'willow-autoplay-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const script = path.join(root, 'codex.cjs');
  const log = path.join(root, 'calls.json');
  await writeFile(script, fakeCodex);
  await writeFile(path.join(root, 'plan.json'), JSON.stringify(plan));
  process.env.FAKE_CODEX_LOG = log;
  process.env.FAKE_CODEX_PLAN = path.join(root, 'plan.json');
  const reports = [];
  const controller = new AbortController();
  return {
    reports,
    controller,
    calls: async () => JSON.parse(await readFile(log, 'utf8')),
    run: options => runAutoPlay({
      codex: process.execPath, codexPrefix: [script], profile: profiles.luna, sessionId, cwd: root,
      report: (type, fields) => reports.push({ type, ...fields }), signal: controller.signal,
      state: async () => ({ started: true, paused: false }), pauseInterval: 5, retryDelay: 5, turnDelay: 5,
      ...options,
    }),
  };
}

test('profiles use Terra medium by default and Luna extra high as the alternative', () => {
  const fresh = codexArguments({ profile: profiles.terra, sessionId });
  assert.deepEqual(fresh.slice(0, 4), ['exec', '--json', '-m', 'gpt-6.1-terra']);
  assert.ok(fresh.includes('model_reasoning_effort="medium"'));
  assert.ok(fresh.includes('sandbox_mode="workspace-write"'));
  const server = fresh.find(arg => arg.startsWith('mcp_servers.mgba-willow='));
  assert.match(server, /server\.mjs/);
  assert.match(server, new RegExp(`WILLOW_AUTOPLAY_SESSION_ID="${sessionId}"`));
  assert.match(server, /required=true/);
  assert.match(fresh.at(-1), new RegExp(sessionId));
  const resumed = codexArguments({ profile: profiles.luna, sessionId, thread: 'thread-9' });
  assert.deepEqual(resumed.slice(0, 5), ['exec', 'resume', '--json', '-m', 'gpt-6-luna']);
  assert.ok(resumed.includes('model_reasoning_effort="xhigh"'));
  assert.equal(resumed.at(-2), 'thread-9');
  assert.match(autoPlayPrompt(sessionId), /never ask questions/);
});

test('turn status and activity come from Codex events', () => {
  assert.deepEqual(turnStatus('Beat the trainer.\nAUTOPLAY: continue'), { status: 'continue', reason: '' });
  assert.deepEqual(turnStatus('AUTOPLAY: blocked Choose a starter'), { status: 'blocked', reason: 'Choose a starter' });
  assert.equal(turnStatus('no status line').status, 'continue');
  assert.equal(describe({ type: 'item.started', item: { type: 'mcp_tool_call', server: 'mgba-willow', tool: 'move_to' } }), 'Using move_to');
  assert.equal(describe({ type: 'item.completed', item: { type: 'agent_message', text: 'I’ll play now.' } }), undefined);
});

test('Auto Play resumes the same Codex thread until the player is needed', async t => {
  const h = await harness(t, [
    { tools: 3, message: 'Walked north.\nAUTOPLAY: continue' },
    { tools: 1, message: 'AUTOPLAY: blocked Pick a nickname' },
  ]);
  await h.run();
  const calls = await h.calls();
  assert.equal(calls.length, 2);
  assert.deepEqual(calls[0].slice(0, 4), ['exec', '--json', '-m', 'gpt-6-luna']);
  assert.deepEqual(calls[1].slice(0, 5), ['exec', 'resume', '--json', '-m', 'gpt-6-luna']);
  assert.ok(calls[1].includes('thread-1'));
  assert.ok(h.reports.some(report => report.type === 'activity' && report.message === 'Using press'));
  assert.equal(h.reports.at(-1).type, 'error');
  assert.match(h.reports.at(-1).message, /Pick a nickname/);
});

test('Auto Play waits while the game is paused and stops idle turns', async t => {
  const h = await harness(t, [
    { actions: [{ tool: 'observe' }], message: 'Nothing to do.' },
    { actions: [{ tool: 'press', status: 'failed' }], message: 'Still nothing.' },
  ]);
  let checks = 0;
  await h.run({ state: async () => ({ started: true, paused: ++checks <= 2 }) });
  assert.ok(h.reports.some(report => report.paused));
  assert.equal((await h.calls()).length, 2);
  assert.match(h.reports.at(-1).message, /without playing/);
});

test('Auto Play retries a failed turn in a fresh thread, then gives up', async t => {
  const h = await harness(t, [{ fail: 'boom' }, { fail: 'boom' }, { fail: 'boom' }]);
  await h.run();
  const calls = await h.calls();
  assert.equal(calls.length, 3);
  assert.ok(calls.every(call => call[1] === '--json'));
  assert.match(h.reports.at(-1).message, /three times in a row: boom/);
});

test('Auto Play stops immediately on usage limits and when the game closes', async t => {
  const h = await harness(t, [{ fail: 'You have hit your usage limit.' }]);
  await h.run();
  assert.equal((await h.calls()).length, 1);
  assert.match(h.reports.at(-1).message, /usage limit/);
  const closed = await harness(t, []);
  await closed.run({ state: async () => null });
  assert.match(closed.reports.at(-1).message, /Open a single-player game/);
});

test('Auto Play times out a stalled Codex turn and retries', async t => {
  const h = await harness(t, [{ hang: true }, { tools: 1, message: 'AUTOPLAY: blocked Test complete' }]);
  await h.run({ activityTimeout: 500 });
  assert.equal((await h.calls()).length, 2);
  assert.ok(h.reports.some(report => report.message.includes('stopped responding')));
  assert.match(h.reports.at(-1).message, /Test complete/);
});
