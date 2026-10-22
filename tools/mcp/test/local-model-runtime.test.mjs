import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, mkdtemp, readdir, rm, mkdir, writeFile, copyFile } from 'node:fs/promises';
import { execFileSync, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import os from 'node:os';
import path from 'node:path';

const config = JSON.parse(await readFile(new URL('../../local-model/runtime.json', import.meta.url), 'utf8'));

async function installationFixture(t) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'mgba-install-test-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const scripts = path.join(root, 'scripts');
  await mkdir(scripts);
  for (const file of ['serve.ps1', 'check-install.ps1', 'launch.ps1', 'session.ps1', 'ensure-node.ps1', 'goals.ps1', 'goals.json']) {
    await copyFile(new URL(`../../local-model/${file}`, import.meta.url), path.join(scripts, file));
  }
  const bytes = Buffer.from('gguf');
  const fixtureConfig = { ...config, model: { ...config.model, filename: 'fixture.gguf', bytes: bytes.length,
    sha256: createHash('sha256').update(bytes).digest('hex') } };
  await writeFile(path.join(scripts, 'runtime.json'), JSON.stringify(fixtureConfig));
  const run = (file, args, input) => spawnSync('powershell.exe', [
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(scripts, file), ...args,
  ], { env: { ...process.env, LOCALAPPDATA: root }, encoding: 'utf8', timeout: 15000, input });
  const check = (device = 'cpu') => {
    const result = run('serve.ps1', ['-Device', device, '-CheckInstalled']);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    return JSON.parse(result.stdout.replace(/^\uFEFF/, ''));
  };
  return { root, scripts, bytes, run, check, model: path.join(root, 'mgba-willow', 'models', 'fixture.gguf'),
    runtime: device => path.join(root, 'mgba-willow-tools', `llama-${config.runtime.version}-${device}`) };
}

async function fakeRuntime(directory, device) {
  await mkdir(directory, { recursive: true });
  for (const file of ['llama-server.exe', 'llama-server-impl.dll', 'llama.dll', 'ggml.dll', 'ggml-base.dll',
    ...(device === 'cuda' ? ['ggml-cuda.dll', 'cudart64_13.dll', 'cublas64_13.dll', 'cublasLt64_13.dll'] : [])]) {
    await writeFile(path.join(directory, file), 'fixture, never executed');
  }
  await writeFile(path.join(directory, 'runtime-ready'), config.runtime[device].sha256);
}

test('Artemis Q4_K_M model and Windows runtimes have pinned URLs, sizes and SHA-256 digests', () => {
  assert.equal(config.model.filename, 'Kev-4B-Artemis-Q4_K_M.gguf');
  assert.equal(config.model.bytes, 2541447616);
  assert.equal(config.model.sha256, 'b8a3a2e8605b0b9f28a7cfb15d7b4a003ad55306c6bca3c055a62bad18b926fd');
  assert.match(config.model.revision, /^[a-f0-9]{40}$/);
  assert.equal(new URL(config.model.url).hostname, 'huggingface.co');
  assert.ok(config.model.url.includes(`/resolve/${config.model.revision}/`));
  assert.equal(new URL(config.node.url).hostname, 'nodejs.org');
  assert.ok(config.node.url.includes(`/dist/${config.node.version}/`));
  for (const asset of [config.model, config.node, config.runtime.cuda, config.runtime.cudaDependencies, config.runtime.cpu]) {
    assert.match(asset.sha256, /^[a-f0-9]{64}$/);
    assert.ok(Number.isSafeInteger(asset.bytes) && asset.bytes > 0);
    assert.equal(new URL(asset.url).protocol, 'https:');
    assert.ok(asset.url.endsWith(`/${asset.filename}`));
  }
  for (const asset of [config.runtime.cuda, config.runtime.cudaDependencies, config.runtime.cpu]) {
    assert.equal(new URL(asset.url).hostname, 'github.com');
    assert.ok(asset.url.includes(`/releases/download/${config.runtime.version}/`));
  }
});

test('Windows launcher exposes stable storage and CUDA/CPU settings without creating project storage',
  { skip: process.platform !== 'win32' }, async t => {
    const root = await mkdtemp(path.join(os.tmpdir(), 'mgba-runtime-test-'));
    t.after(() => rm(root, { recursive: true, force: true }));
    const script = fileURLToPath(new URL('../../local-model/serve.ps1', import.meta.url));
    const plan = extra => JSON.parse(execFileSync('powershell.exe', [
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-ShowPaths', ...extra,
    ], { env: { ...process.env, LOCALAPPDATA: root }, encoding: 'utf8', timeout: 15000 }).replace(/^\uFEFF/, ''));
    const cuda = plan([]);
    const argument = (settings, flag) => settings.arguments[settings.arguments.indexOf(flag) + 1];
    assert.equal(cuda.model, path.join(root, 'mgba-willow', 'models', config.model.filename));
    assert.equal(cuda.runtime, path.join(root, 'mgba-willow-tools', `llama-${config.runtime.version}-cuda`));
    assert.equal(cuda.device, 'cuda');
    for (const [flag, expected] of Object.entries({ '--host': '127.0.0.1', '--alias': 'kev-latest',
      '--port': '8008', '-c': '8192', '-np': '1', '-ub': '4096', '-b': '4096', '-ngl': '20', '--cache-ram': '0' })) {
      assert.equal(argument(cuda, flag), expected);
    }
    const cpu = plan(['-Device', 'cpu', '-GpuLayers', '99']);
    assert.equal(cpu.model, cuda.model, 'CPU mode reuses the exact same model file');
    assert.equal(argument(cpu, '-ngl'), '0');
    const custom = plan(['-GpuLayers', '12', '-ContextSize', '4096', '-Port', '18009']);
    assert.equal(argument(custom, '-ngl'), '12');
    assert.equal(argument(custom, '-c'), '4096');
    assert.equal(argument(custom, '--port'), '18009');
    // PowerShell itself may create a Microsoft cache below LOCALAPPDATA.
    assert.deepEqual((await readdir(root)).filter(name => name.startsWith('mgba-')), [],
      'ShowPaths must not download, install or acquire a project lock');
  });

test('installation check detects missing/corrupt model and incomplete CPU/CUDA runtimes without downloads',
  { skip: process.platform !== 'win32' }, async t => {
    const f = await installationFixture(t);
    const missing = f.check();
    assert.equal(missing.installed, false);
    assert.equal(missing.modelStatus, 'missing');
    assert.equal(missing.runtimeInstalled, false);
    assert.deepEqual((await readdir(f.root)).filter(name => name.startsWith('mgba-')), [],
      'Checking a fresh PC must not create model/runtime storage');
    await mkdir(path.dirname(f.model), { recursive: true });
    await writeFile(f.model, 'oops'); // Same size as valid fixture; must fail SHA-256.
    assert.equal(f.check().modelStatus, 'invalid');
    await writeFile(f.model, f.bytes);
    assert.equal(f.check().modelStatus, 'verified');
    assert.equal(f.check().installed, false);
    await fakeRuntime(f.runtime('cpu'), 'cpu');
    assert.equal(f.check().installed, true);
    await rm(path.join(f.runtime('cpu'), 'llama.dll'));
    assert.equal(f.check().runtimeInstalled, false);
    await writeFile(path.join(f.runtime('cpu'), 'llama.dll'), 'fixture');
    await writeFile(path.join(f.runtime('cpu'), 'runtime-ready'), 'wrong package hash');
    assert.equal(f.check().runtimeInstalled, false);
    await fakeRuntime(f.runtime('cuda'), 'cuda');
    assert.equal(f.check('cuda').installed, true);
    await rm(path.join(f.runtime('cuda'), 'cublasLt64_13.dll'));
    assert.equal(f.check('cuda').runtimeInstalled, false, 'CUDA dependencies are required, not only the EXE');
  });

test('manual diagnostic setup can still be declined without downloading',
  { skip: process.platform !== 'win32' }, async t => {
    const f = await installationFixture(t);
    const optional = f.run('check-install.ps1', ['-Device', 'cpu', '-OfferInstall'], 'n\n');
    assert.equal(optional.status, 0, optional.stdout + optional.stderr);
    assert.match(optional.stdout, /optional for normal emulator use/);
    assert.match(optional.stdout, /not installed/);
    const required = f.run('check-install.ps1', ['-Device', 'cpu', '-OfferInstall', '-RequireInstalled'], '\n');
    assert.equal(required.status, 1, required.stdout + required.stderr);
    assert.deepEqual((await readdir(f.root)).filter(name => name.startsWith('mgba-')), [],
      'Declining must not download or create model/runtime storage');
  });

test('accepting first-run setup invokes download-only then rechecks, and later launches reuse installation',
  { skip: process.platform !== 'win32' }, async t => {
    const f = await installationFixture(t);
    // A local fixture installer records consent without network access or executable downloads.
    await writeFile(path.join(f.scripts, 'serve.ps1'), `
param([string]$Device, [switch]$CheckInstalled, [switch]$DownloadOnly)
$marker = Join-Path $env:LOCALAPPDATA 'installed-fixture'
if ($DownloadOnly) { Add-Content -LiteralPath $marker -Value 'download-only'; exit 0 }
if ($CheckInstalled) {
    $installed = Test-Path -LiteralPath $marker
    @{ installed = $installed; modelInstalled = $installed; runtimeInstalled = $installed;
       modelStatus = 'missing'; model = 'fixture.gguf'; runtime = 'fixture-runtime'; device = $Device } | ConvertTo-Json
    exit 0
}
exit 9
`);
    const accepted = f.run('check-install.ps1', ['-Device', 'cpu', '-OfferInstall', '-RequireInstalled'], 'yes\n');
    assert.equal(accepted.status, 0, accepted.stdout + accepted.stderr);
    assert.match(accepted.stdout, /installed and ready/);
    assert.equal((await readFile(path.join(f.root, 'installed-fixture'), 'utf8')).trim(), 'download-only');
    const reused = f.run('check-install.ps1', ['-Device', 'cpu', '-OfferInstall'], '');
    assert.equal(reused.status, 0, reused.stdout + reused.stderr);
    assert.match(reused.stdout, /runtime is ready/);
    assert.equal((await readFile(path.join(f.root, 'installed-fixture'), 'utf8')).trim(), 'download-only',
      'An installed model must not trigger another download');
  });
