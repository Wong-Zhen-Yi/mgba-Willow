import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import willow, { blockedReason, codexAccessToken, codexProvider, toolContent, trimContext, wrapUp } from '../pi-extension.mjs';

const jwt = exp => `x.${Buffer.from(JSON.stringify({ exp })).toString('base64url')}.y`;

test('the Codex sign-in is read without touching the refresh token', async t => {
  const home = await mkdtemp(path.join(os.tmpdir(), 'willow-codex-home-'));
  t.after(() => rm(home, { recursive: true, force: true }));
  assert.equal(codexAccessToken(home), undefined);
  const fresh = jwt(Math.floor(Date.now() / 1000) + 3600);
  await writeFile(path.join(home, 'auth.json'), JSON.stringify({ tokens: { access_token: fresh, refresh_token: 'r' } }));
  assert.equal(codexAccessToken(home), fresh);
  await writeFile(path.join(home, 'auth.json'), JSON.stringify({ tokens: { access_token: jwt(1000) } }));
  assert.equal(codexAccessToken(home), undefined);
});

test('Auto Play blocks the shell and edits outside the progress notes', () => {
  const cwd = path.resolve('/repo');
  assert.match(blockedReason('bash', { command: 'ls' }, cwd), /shell/);
  assert.equal(blockedReason('edit', { path: 'CURRENT_HANDOFF.md' }, cwd), undefined);
  assert.equal(blockedReason('write', { path: '@STORY_PROGRESS_NOTES.md' }, cwd), undefined);
  assert.equal(blockedReason('edit', { path: path.join(cwd, 'POKEMON_EMERALD_OBJECTIVES.md') }, cwd), undefined);
  assert.match(blockedReason('edit', { path: 'src/gba/gba.c' }, cwd), /may only edit/);
  assert.match(blockedReason('write', { path: '../CURRENT_HANDOFF.md' }, cwd), /may only edit/);
  assert.match(blockedReason('write', { path: 'doc/CURRENT_HANDOFF.md' }, cwd), /may only edit/);
  assert.equal(blockedReason('read', { path: 'doc/willow-control.md' }, cwd), undefined);
  assert.equal(blockedReason('press', { button: 'A' }, cwd), undefined);
});

test('old screenshots and game state shrink before each model call', () => {
  const tools = new Set(['observe']);
  const result = index => ({ role: 'toolResult', toolName: 'observe', content: [
    { type: 'text', text: `${index}:${'x'.repeat(2000)}` }, { type: 'image', data: 'A', mimeType: 'image/png' },
  ] });
  const read = { role: 'toolResult', toolName: 'read', content: [{ type: 'text', text: 'y'.repeat(5000) }] };
  const messages = [read, ...Array.from({ length: 15 }, (_, index) => result(index))];
  const trimmed = trimContext(structuredClone(messages), tools);
  assert.deepEqual(trimmed[0], read);
  const images = trimmed.slice(1).map(message => message.content[1].type);
  assert.deepEqual(images, [...Array(12).fill('text'), 'image', 'image', 'image']);
  const lengths = trimmed.slice(1).map(message => message.content[0].text.length);
  assert.ok(lengths.slice(0, 3).every(length => length < 1300));
  assert.ok(lengths.slice(3).every(length => length > 2000));
  // Trimming is stable: running it again on the trimmed copy changes nothing.
  assert.deepEqual(trimContext(structuredClone(trimmed), tools), trimmed);
});

test('MCP text and images become Pi tool content', () => {
  assert.deepEqual(toolContent({ content: [
    { type: 'text', text: '{"ok":true}' },
    { type: 'image', mimeType: 'image/png', data: 'AAAA' },
    { type: 'resource', resource: {} },
  ] }), [{ type: 'text', text: '{"ok":true}' }, { type: 'image', mimeType: 'image/png', data: 'AAAA' }]);
  assert.deepEqual(toolContent({ content: [], structuredContent: { a: 1 } }), [{ type: 'text', text: '{"a":1}' }]);
});

test('the extension registers every mgba-willow tool as a sequential Pi tool', async t => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'willow-pi-sessions-'));
  const codexHome = await mkdtemp(path.join(os.tmpdir(), 'willow-pi-codex-'));
  const saved = { dir: process.env.MGBA_WILLOW_SESSION_DIR, home: process.env.CODEX_HOME };
  process.env.MGBA_WILLOW_SESSION_DIR = directory;
  process.env.CODEX_HOME = codexHome;
  await writeFile(path.join(codexHome, 'auth.json'), JSON.stringify({ tokens: { access_token: jwt(Math.floor(Date.now() / 1000) + 3600) } }));
  const tools = new Map();
  const handlers = new Map();
  const providers = new Map();
  const steered = [];
  const pi = {
    registerTool: tool => tools.set(tool.name, tool),
    registerProvider: (name, config) => providers.set(name, config),
    on: (name, handler) => handlers.set(name, handler),
    sendUserMessage: (text, options) => steered.push({ text, options }),
  };
  process.env.WILLOW_PI_TURN_ACTIONS = '2';
  t.after(async () => {
    await handlers.get('session_shutdown')?.();
    delete process.env.WILLOW_PI_TURN_ACTIONS;
    process.env.MGBA_WILLOW_SESSION_DIR = saved.dir;
    process.env.CODEX_HOME = saved.home;
    if (saved.dir === undefined) delete process.env.MGBA_WILLOW_SESSION_DIR;
    if (saved.home === undefined) delete process.env.CODEX_HOME;
    await rm(directory, { recursive: true, force: true });
    await rm(codexHome, { recursive: true, force: true });
  });
  await willow(pi);

  assert.deepEqual(providers.get(codexProvider).models.map(model => model.id), ['gpt-6-luna', 'gpt-6.1-terra']);
  assert.equal(providers.get(codexProvider).api, 'openai-codex-responses');
  for (const name of ['connect', 'observe', 'press', 'move_to', 'disconnect']) assert.ok(tools.has(name), name);
  assert.ok([...tools.values()].every(tool => tool.executionMode === 'sequential' && tool.parameters.type === 'object'));
  assert.ok('session_id' in tools.get('connect').parameters.properties);

  const sessions = await tools.get('list_sessions').execute('1', {}, undefined);
  assert.deepEqual(JSON.parse(sessions.content[0].text), { sessions: [] });
  await assert.rejects(tools.get('connect').execute('2', { session_id: 'missing' }, undefined), /session/i);

  const prompt = await handlers.get('before_agent_start')({ systemPrompt: 'base' });
  assert.match(prompt.systemPrompt, /^base\n\n# mgba-willow tools\n\nControl a visible mGBA game/);
  const call = name => handlers.get('tool_call')({ toolName: name, input: {} }, { cwd: directory });
  assert.deepEqual(call('bash'), { block: true, reason: 'Auto Play cannot run shell commands.' });

  // Turn budget of 2: a steer at 2 game actions, game input refused at 4.
  const end = (toolName, isError = false) => handlers.get('tool_execution_end')({ toolName, isError });
  end('press'); end('observe'); end('move_to', true);
  assert.equal(steered.length, 0);
  end('move_to');
  assert.deepEqual(steered, [{ text: wrapUp(2), options: { deliverAs: 'steer' } }]);
  end('press');
  assert.equal(call('press'), undefined);
  end('act');
  assert.match(call('press').reason, /refused until the next turn/);
  assert.equal(call('disconnect'), undefined);
  assert.equal(steered.length, 1);
});
