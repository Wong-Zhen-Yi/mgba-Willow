import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { describe, piArguments, piCommand, profiles, readPiEvent, runAutoPlay } from '../autoplay.mjs';

const sessionId = '0f3c3a52-8a6e-4d8e-9a43-0f1b2c3d4e5f';

// A stand-in for Pi's cli.js: prints one `--mode json` turn per call and logs its arguments.
const fakePi = `
const fs = require('node:fs');
const log = process.env.FAKE_PI_LOG;
const calls = fs.existsSync(log) ? JSON.parse(fs.readFileSync(log, 'utf8')) : [];
calls.push({ args: process.argv.slice(2), session: process.env.WILLOW_AUTOPLAY_SESSION_ID });
fs.writeFileSync(log, JSON.stringify(calls));
const turn = JSON.parse(fs.readFileSync(process.env.FAKE_PI_PLAN, 'utf8'))[calls.length - 1];
const emit = event => process.stdout.write(JSON.stringify(event) + '\\n');
const flag = name => process.argv[process.argv.indexOf(name) + 1];
if (turn.saved) {
  fs.mkdirSync(flag('--session-dir'), { recursive: true });
  fs.writeFileSync(require('node:path').join(flag('--session-dir'), '2026-11-26_' + flag('--session-id') + '.jsonl'), '{}');
}
emit({ type: 'session', id: 'pi-session' });
for (const [index, tool] of (turn.tools ?? []).entries()) {
  emit({ type: 'tool_execution_start', toolCallId: String(index), toolName: tool.name, args: {} });
  emit({ type: 'tool_execution_end', toolCallId: String(index), toolName: tool.name, result: {}, isError: Boolean(tool.error) });
}
const message = { role: 'assistant', content: [{ type: 'thinking', thinking: 'hm' }, { type: 'text', text: turn.message ?? '' }],
  stopReason: turn.error ? 'error' : 'stop', errorMessage: turn.error, usage: { totalTokens: 1500 } };
emit({ type: 'message_end', message });
emit({ type: 'agent_end', messages: [message] });
`;

async function harness(t, plan) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'willow-autoplay-pi-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const script = path.join(root, 'cli.cjs');
  const log = path.join(root, 'calls.json');
  await writeFile(script, fakePi);
  await writeFile(path.join(root, 'plan.json'), JSON.stringify(plan));
  process.env.FAKE_PI_LOG = log;
  process.env.FAKE_PI_PLAN = path.join(root, 'plan.json');
  const reports = [];
  return {
    reports,
    calls: async () => JSON.parse(await readFile(log, 'utf8')),
    run: options => runAutoPlay({
      program: script, profile: profiles['pi-luna'], sessionId, cwd: root, piSessionDir: path.join(root, 'sessions'),
      report: (type, fields) => reports.push({ type, ...fields }), signal: new AbortController().signal,
      state: async () => ({ started: true, paused: false }), pauseInterval: 5, retryDelay: 5, turnDelay: 5,
      ...options,
    }),
  };
}

const sessionOf = call => call.args[call.args.indexOf('--session-id') + 1];

test('Pi profiles load the Willow extension with Luna extra high and no shell', () => {
  assert.equal(profiles['pi-luna'].harness, 'pi');
  const args = piArguments({ profile: profiles['pi-luna'], sessionId, session: 's-1', resume: false, sessionDir: 'dir' });
  assert.deepEqual(args.slice(0, 3), ['--mode', 'json', '-p']);
  assert.match(args[args.indexOf('-e') + 1], /pi-extension\.mjs$/);
  assert.ok(args.includes('--no-extensions'));
  assert.equal(args[args.indexOf('--exclude-tools') + 1], 'bash');
  assert.equal(args[args.indexOf('--model') + 1], 'willow-codex/gpt-6-luna');
  assert.equal(args[args.indexOf('--thinking') + 1], 'xhigh');
  assert.equal(args[args.indexOf('--session-id') + 1], 's-1');
  assert.match(args.at(-1), /never ask questions/);
  const resumed = piArguments({ profile: profiles['pi-terra'], sessionId, session: 's-1', resume: true, sessionDir: 'dir' });
  assert.equal(resumed[resumed.indexOf('--thinking') + 1], 'medium');
  assert.match(resumed.at(-1), /^Continue Willow Auto Play/);
});

test('Pi runs from its npm shim directory under the current Node', async t => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'willow-pi-shim-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const cli = path.join(root, 'node_modules', '@earendil-works', 'pi-coding-agent', 'dist', 'cli.js');
  await mkdir(path.dirname(cli), { recursive: true });
  await writeFile(cli, '');
  assert.deepEqual(piCommand(path.join(root, 'pi.cmd')), { command: process.execPath, prefix: [cli] });
  assert.deepEqual(piCommand('/opt/pi/cli.mjs'), { command: process.execPath, prefix: ['/opt/pi/cli.mjs'] });
  const bare = path.join(root, 'elsewhere', 'pi');
  assert.deepEqual(piCommand(bare), { command: bare, prefix: [] });
});

test('Pi events count game actions, track tokens and surface model errors', () => {
  const result = { message: '', tools: 0, tokens: 0, error: '' };
  readPiEvent({ type: 'session', id: 'abc' }, result);
  readPiEvent({ type: 'tool_execution_end', toolName: 'press', isError: false }, result);
  readPiEvent({ type: 'tool_execution_end', toolName: 'observe', isError: false }, result);
  readPiEvent({ type: 'tool_execution_end', toolName: 'move_to', isError: true }, result);
  readPiEvent({ type: 'message_end', message: {
    role: 'assistant', content: [{ type: 'text', text: 'AUTOPLAY: continue' }], stopReason: 'stop', usage: { totalTokens: 10 },
  } }, result);
  assert.deepEqual(result, { thread: 'abc', message: 'AUTOPLAY: continue', tools: 1, tokens: 10, error: '' });
  readPiEvent({ type: 'message_end', message: {
    role: 'assistant', content: [], stopReason: 'error', errorMessage: 'No API key for provider: willow-codex',
  } }, result);
  assert.match(result.error, /No API key/);
  assert.equal(result.message, 'AUTOPLAY: continue');
  assert.equal(describe({ type: 'tool_execution_start', toolName: 'move_to' }), 'Using move_to');
  assert.match(describe({ type: 'auto_retry_start', attempt: 1, maxAttempts: 3 }), /Retrying/);
});

test('Pi Auto Play keeps one session across turns until the player is needed', async t => {
  const h = await harness(t, [
    { tools: [{ name: 'press' }, { name: 'move_to' }], message: 'Walked.\nAUTOPLAY: continue' },
    { tools: [{ name: 'press' }], message: 'AUTOPLAY: blocked Pick a nickname' },
  ]);
  await h.run();
  const calls = await h.calls();
  assert.equal(calls.length, 2);
  assert.ok(calls.every(call => call.session === sessionId));
  assert.equal(sessionOf(calls[0]), sessionOf(calls[1]));
  assert.match(calls[0].args.at(-1), /never ask questions/);
  assert.match(calls[1].args.at(-1), /^Continue Willow Auto Play/);
  assert.ok(h.reports.some(report => report.message === 'Using move_to'));
  assert.ok(h.reports.some(report => report.message === 'Turn 1 done (2 game actions, 2k tokens)'));
  assert.match(h.reports.at(-1).message, /Pick a nickname/);
});

test('Pi Auto Play stops on a missing sign-in and starts a new session after other errors', async t => {
  const signIn = await harness(t, [{ error: 'No API key for provider: willow-codex' }]);
  await signIn.run();
  assert.equal((await signIn.calls()).length, 1);
  assert.match(signIn.reports.at(-1).message, /Codex sign-in/);
  const flaky = await harness(t, [{ error: 'socket hang up' }, { tools: [{ name: 'press' }], message: 'AUTOPLAY: blocked done' }]);
  await flaky.run();
  const calls = await flaky.calls();
  assert.notEqual(sessionOf(calls[0]), sessionOf(calls[1]));
  assert.match(calls[1].args.at(-1), /never ask questions/);
  assert.ok(flaky.reports.some(report => /Pi stopped unexpectedly \(socket hang up\)/.test(report.message)));
});

test('Pi Auto Play continues a saved session once after a dropped connection', async t => {
  const h = await harness(t, [
    { saved: true, tools: [{ name: 'press' }], error: 'WebSocket closed 1012' },
    { saved: true, error: 'WebSocket closed 1012' },
    { saved: true, error: 'WebSocket closed 1012' },
  ]);
  await h.run();
  const calls = await h.calls();
  assert.equal(calls.length, 3);
  assert.equal(sessionOf(calls[0]), sessionOf(calls[1]));
  assert.match(calls[1].args.at(-1), /^Continue Willow Auto Play/);
  assert.notEqual(sessionOf(calls[1]), sessionOf(calls[2]));
  assert.match(calls[2].args.at(-1), /never ask questions/);
  assert.match(h.reports.at(-1).message, /three times in a row: WebSocket closed 1012/);
});
