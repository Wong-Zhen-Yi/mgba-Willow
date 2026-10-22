import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, copyFile, writeFile, readFile, readdir, rm } from 'node:fs/promises';
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import { randomUUID } from 'node:crypto';
import os from 'node:os';
import path from 'node:path';

const windows = { skip: process.platform !== 'win32' };
const catalog = JSON.parse(await readFile(new URL('../../local-model/goals.json', import.meta.url), 'utf8'));

async function fixture(t) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'mgba-opt-in-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const scripts = path.join(root, 'scripts');
  await mkdir(scripts);
  for (const file of ['launch.ps1', 'goals.ps1', 'goals.json']) {
    await copyFile(new URL(`../../local-model/${file}`, import.meta.url), path.join(scripts, file));
  }
  const session = { session_id: randomUUID(), game_id: randomUUID(), started: true, title: 'Synthetic game' };
  await writeFile(path.join(scripts, 'session.ps1'), `
function Get-LoadedGame($EmulatorPid) {
    $file = Join-Path $env:LOCALAPPDATA 'loaded.json'
    if (Test-Path -LiteralPath $file) { return Get-Content -LiteralPath $file -Raw | ConvertFrom-Json }
    return $null
}
function Test-CurrentGame($Emulator, $Session) { return Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'current') }
`);
  await writeFile(path.join(scripts, 'check-install.ps1'), `
param([string]$Device, [switch]$Json)
Add-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'events') -Value 'check'
@{ installed = (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'installed')); device = 'cpu' } | ConvertTo-Json
`);
  await writeFile(path.join(scripts, 'serve.ps1'), `
param([string]$Device, [switch]$DownloadOnly)
if (-not $DownloadOnly) { throw 'This fixture must never start a server' }
Add-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'events') -Value 'download-only'
Set-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'installed') -Value 'verified fixture'
if (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'cancel-setup')) {
    Remove-Item -LiteralPath (Join-Path $env:LOCALAPPDATA 'current')
}
`);
  await writeFile(path.join(scripts, 'ensure-node.ps1'), `
Add-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'events') -Value 'node'
# Simulate the user changing/closing the game while player setup completes.
Remove-Item -LiteralPath (Join-Path $env:LOCALAPPDATA 'current')
Write-Output 'fixture-node.exe'
`);
  const run = (extra = []) => spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive',
    '-ExecutionPolicy', 'Bypass', '-File', path.join(scripts, 'launch.ps1'),
    '-EmulatorPid', String(process.pid), '-SessionId', session.session_id, '-GameId', session.game_id,
    '-Device', 'cpu', ...extra], { env: { ...process.env, LOCALAPPDATA: root }, encoding: 'utf8', timeout: 15000 });
  const load = async () => {
    await writeFile(path.join(root, 'loaded.json'), JSON.stringify(session));
    await writeFile(path.join(root, 'current'), 'current game');
  };
  return { root, session, run, load };
}

test('goal presets include story and training instructions within bridge limits', () => {
  assert.equal(catalog.version, 1);
  assert.ok(catalog.goals.some(goal => goal.id === 'progress-story' && goal.label === 'Progress the story'));
  assert.ok(catalog.goals.some(goal => goal.id === 'level-up-pokemon' && goal.label === 'Level up Pokémon'));
  const ids = new Set();
  for (const goal of catalog.goals) {
    assert.match(goal.id, /^[a-z][a-z0-9-]{0,47}$/);
    assert.ok(!ids.has(goal.id));
    ids.add(goal.id);
    assert.ok(goal.label.trim() && goal.label.length <= 80);
    assert.ok(goal.goal.trim() && goal.goal.length <= 512);
    assert.match(goal.goal, /Stop|stop/);
  }
});

test('invalid or duplicate presets stop setup before model checks or downloads', windows, async t => {
  const f = await fixture(t);
  await f.load();
  for (const contents of ['not JSON', JSON.stringify({ version: 1, goals: [] }),
    JSON.stringify({ version: 1, goals: [catalog.goals[0], catalog.goals[0]] }),
    JSON.stringify({ version: 1, goals: [{ ...catalog.goals[0], goal: 'x'.repeat(513) }] })]) {
    await writeFile(path.join(f.root, 'scripts', 'goals.json'), contents);
    const result = f.run(['-GoalId', 'progress-story']);
    assert.equal(result.status, 1, result.stdout + result.stderr);
    assert.ok(!(await readdir(f.root)).includes('events'));
  }
});

test('only one double-click launcher remains and it never invokes model setup', async () => {
  const root = new URL('../../../', import.meta.url);
  assert.deepEqual((await readdir(root)).filter(file => file.endsWith('.cmd')), ['Launch mGBA.cmd']);
  const cmd = await readFile(new URL('Launch mGBA.cmd', root), 'utf8');
  assert.doesNotMatch(cmd, /-File[^\r\n]*(?:check-install|local-model\\(?:launch|serve))\.ps1/i);
  const launcher = await readFile(new URL('../../launch-windows.ps1', import.meta.url), 'utf8');
  assert.match(launcher, /MGBA_LOCAL_MODEL_SCRIPT/);
  assert.doesNotMatch(launcher, /Start-Process[^\r\n]*(?:local-model|serve\.ps1)/);
});

test('opt-in helper refuses an absent or stale game before checks/downloads/runtime storage', windows, async t => {
  const f = await fixture(t);
  let result = f.run();
  assert.equal(result.status, 1, result.stdout + result.stderr);
  assert.match(result.stdout, /selected game is no longer open/);
  await f.load();
  const unknownGoal = f.run(['-GoalId', 'unknown-preset']);
  assert.equal(unknownGoal.status, 1, unknownGoal.stdout + unknownGoal.stderr);
  assert.match(unknownGoal.stdout, /Unknown gameplay goal preset/);
  await writeFile(path.join(f.root, 'loaded.json'), JSON.stringify({ ...f.session, game_id: randomUUID() }));
  result = f.run();
  assert.equal(result.status, 1, result.stdout + result.stderr);
  const files = await readdir(f.root);
  assert.ok(!files.includes('events'), 'Even installation checks must wait for a valid opt-in game');
  assert.ok(!files.includes('mgba-willow-tools'));
});

test('explicit enable installs missing model automatically, reuses installation, and never acts on a replaced game', windows, async t => {
  const f = await fixture(t);
  await f.load();
  let result = f.run();
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.match(result.stdout, /First-run setup/);
  assert.doesNotMatch(result.stdout, /Starting Kev|gameplay goal/);
  assert.deepEqual((await readFile(path.join(f.root, 'events'), 'utf8')).trim().split(/\r?\n/),
    ['check', 'download-only', 'node']);
  await f.load();
  result = f.run();
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.match(result.stdout, /model verified/);
  assert.deepEqual((await readFile(path.join(f.root, 'events'), 'utf8')).trim().split(/\r?\n/),
    ['check', 'download-only', 'node', 'check', 'node']);
});

for (const presetId of [null, 'progress-story', 'level-up-pokemon', 'console-preset']) {
  test(`enabled helper accepts ${presetId || 'custom goal'} and leaves borrowed server running`, windows, async t => {
    const f = await fixture(t);
    await f.load();
    await writeFile(path.join(f.root, 'installed'), 'verified fixture');
    await writeFile(path.join(f.root, 'scripts', 'ensure-node.ps1'), `
Write-Host 'Fixture player runtime setup progress'
Write-Output '${process.execPath.replaceAll("'", "''")}'
`);
    await writeFile(path.join(f.root, 'scripts', 'cli.mjs'), `
import { writeFileSync } from 'node:fs';
import path from 'node:path';
writeFileSync(path.join(process.env.LOCALAPPDATA, 'player-args.json'), JSON.stringify(process.argv.slice(2)));
`);
    const server = http.createServer((req, res) => {
      assert.equal(req.url, '/v1/models');
      res.setHeader('content-type', 'application/json');
      res.end(JSON.stringify({ data: [{ id: 'kev-latest' }] }));
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    t.after(() => { server.closeAllConnections(); server.close(); });
    const origin = `http://127.0.0.1:${server.address().port}`;
    const child = spawn('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
      path.join(f.root, 'scripts', 'launch.ps1'), '-EmulatorPid', String(process.pid),
      '-SessionId', f.session.session_id, '-GameId', f.session.game_id, '-Device', 'cpu',
      ...(presetId && presetId !== 'console-preset' ? ['-GoalId', presetId] : [])],
    { env: { ...process.env, LOCALAPPDATA: f.root, KEV_BASE_URL: origin } });
    t.after(() => child.kill());
    let output = '';
    child.stdout.on('data', chunk => { output += chunk; });
    child.stderr.on('data', chunk => { output += chunk; });
    const exited = new Promise((resolve, reject) => { child.once('exit', resolve); child.once('error', reject); });
    child.stdin.end(presetId === 'console-preset' ? '1\n0\n' : presetId ? '0\n' : 'c\nSynthetic goal\n0\n');
    const started = Date.now();
    while (!output.includes('Manual control remains available') && Date.now() - started < 12000) {
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    assert.match(output, /Manual control remains available/);
    assert.match(output, /Reusing the ready local Kev server/);
    assert.match(output, /Fixture player runtime setup progress/);
    const expectedGoal = presetId
      ? catalog.goals.find(goal => goal.id === (presetId === 'console-preset' ? 'progress-story' : presetId)).goal
      : 'Synthetic goal';
    assert.deepEqual(JSON.parse(await readFile(path.join(f.root, 'player-args.json'), 'utf8')),
      ['--session', f.session.session_id, '--game-id', f.session.game_id, '--goal', expectedGoal]);
    await rm(path.join(f.root, 'current'));
    assert.equal(await exited, 0, output);
    const response = await fetch(`${origin}/v1/models`);
    assert.equal(response.status, 200, 'Borrowed server must not be stopped with the helper');
  });
}

test('closing/changing game during model setup prevents player setup and server start', windows, async t => {
  const f = await fixture(t);
  await f.load();
  await writeFile(path.join(f.root, 'cancel-setup'), 'cancel');
  const result = f.run();
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.doesNotMatch(result.stdout, /Starting Kev|gameplay goal/);
  assert.deepEqual((await readFile(path.join(f.root, 'events'), 'utf8')).trim().split(/\r?\n/),
    ['check', 'download-only']);
});
