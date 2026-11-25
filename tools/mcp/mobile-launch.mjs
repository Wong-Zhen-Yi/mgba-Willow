import { execFile, spawn } from 'node:child_process';
import { once } from 'node:events';
import https from 'node:https';
import { access } from 'node:fs/promises';
import { randomBytes } from 'node:crypto';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { isIPv4 } from 'node:net';
import { Resolver } from 'node:dns/promises';

const [pidArgument, tailscale] = process.argv.slice(2);
const pid = Number(pidArgument);
const requestedPort = process.env.WILLOW_MOBILE_PORT === undefined ? undefined : Number(process.env.WILLOW_MOBILE_PORT);
const requestedAccess = process.env.WILLOW_MOBILE_ACCESS;
// Tailscale only lets Funnel listen on these ports.
const funnelPorts = [443, 8443, 10000];
const exec = promisify(execFile);
const isTailscaleIPv4 = ip => isIPv4(ip) && ip.startsWith('100.') && (+ip.split('.')[1] & 0xc0) === 64;
let port;
let serve;
let mobile;
let monitor;
let stopping = false;
const setup = new AbortController();

function report(type, fields) {
  process.stdout.write(JSON.stringify({ type, ...fields }) + '\n');
}

function stop() {
  stopping = true;
  process.stdin.destroy();
  setup.abort();
  clearInterval(monitor);
  mobile?.close();
  serve?.kill();
}

function emulatorRunning() {
  try { process.kill(pid, 0); return true; }
  catch { return false; }
}

async function listen(server) {
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  return server.address().port;
}

async function tailscaleCommand(...args) {
  const { stdout } = await exec(tailscale, args, { windowsHide: true, timeout: 4000 });
  return stdout.trim();
}

async function installDependencies() {
  const directory = fileURLToPath(new URL('.', import.meta.url));
  const modules = ['qrcode', 'ws'].map(name => path.join(directory, 'node_modules', name, 'package.json'));
  try {
    await Promise.all(modules.map(file => access(file)));
  } catch {
    report('progress', { message: 'Installing Willow Mobile dependencies…' });
    const npm = path.join(path.dirname(process.execPath), 'node_modules', 'npm', 'bin', 'npm-cli.js');
    try {
      await exec(process.execPath, [npm, 'ci', '--ignore-scripts', '--no-audit', '--no-fund'], {
        cwd: directory, signal: setup.signal, windowsHide: true, timeout: 120000,
      });
    } catch {
      throw new Error('Could not install mobile dependencies. Check your internet connection and Node.js installation, then retry.');
    }
  }
}

async function tailnetStatus() {
  try { return JSON.parse(await tailscaleCommand('status', '--json')); }
  catch { throw new Error('Connect the Windows Tailscale app to your tailnet first.'); }
}

async function serveStatus() {
  return JSON.parse((await tailscaleCommand('serve', 'status', '--json')) || '{}') ?? {};
}

function portInUse(config, candidate) {
  return [config, ...Object.values(config.Foreground ?? {})].some(record => record.TCP?.[candidate]);
}

// funnel: public ts.net name, so the phone needs no Tailscale DNS; tailnet: private ts.net
// name (phone must use MagicDNS); http: numeric tailnet IP without TLS. Tailscale issues
// and renews the certificate for both HTTPS modes.
async function chooseRoute(status) {
  const host = status.Self?.DNSName?.replace(/\.$/, '');
  const ip = status.Self?.TailscaleIPs?.find(isTailscaleIPv4);
  if (!ip) throw new Error('Tailscale did not provide an IPv4 address.');
  const certificate = Boolean(host && status.CertDomains?.includes(host));
  const funnel = certificate && 'funnel' in (status.Self?.CapMap ?? {});
  const access = requestedAccess ?? (funnel ? 'funnel' : certificate ? 'tailnet' : 'http');
  if (!['funnel', 'tailnet', 'http'].includes(access)) {
    throw new Error('WILLOW_MOBILE_ACCESS must be funnel, tailnet or http.');
  }
  if (access !== 'http' && !certificate) {
    throw new Error('Turn on HTTPS certificates in the Tailscale admin console (DNS page), then retry.');
  }
  if (access === 'funnel' && !funnel) {
    throw new Error('Allow Tailscale Funnel for this PC in your tailnet policy, or set WILLOW_MOBILE_ACCESS=tailnet.');
  }
  const config = await serveStatus();
  if (access === 'funnel') {
    if (requestedPort !== undefined && !funnelPorts.includes(requestedPort)) {
      throw new Error(`For Funnel, WILLOW_MOBILE_PORT must be ${funnelPorts.join(', ')}.`);
    }
    port = (requestedPort === undefined ? funnelPorts : [requestedPort]).find(candidate => !portInUse(config, candidate));
    if (!port) throw new Error(`Tailscale Funnel ports ${funnelPorts.join(', ')} are all in use. Free one, then retry.`);
  } else {
    port = requestedPort ?? 9444;
    if (!Number.isInteger(port) || port < 1024 || port > 65535) {
      throw new Error('WILLOW_MOBILE_PORT must be a port between 1024 and 65535.');
    }
    if (portInUse(config, port)) {
      throw new Error(`Tailscale port ${port} is already in use. Set WILLOW_MOBILE_PORT to a free port.`);
    }
  }
  const suffix = port === 443 ? '' : `:${port}`;
  return access === 'http' ?
    { access, origin: `http://${ip}${suffix}`, ip } :
    { access, origin: `https://${host}${suffix}`, ip, host };
}

async function startForward(localPort, { access, host }) {
  const address = `127.0.0.1:${localPort}`;
  const key = `${host}:${port}`;
  const forwarded = record => access === 'http' ?
    record.TCP?.[port]?.TCPForward === address :
    record.Web?.[key]?.Handlers?.['/']?.Proxy === `http://${address}` &&
      (access !== 'funnel' || record.AllowFunnel?.[key] === true);
  // Foreground Serve/Funnel removes this one forward when the CLI exits.
  const args = access === 'http' ? ['serve', `--tcp=${port}`, `tcp://${address}`] :
    [access === 'funnel' ? 'funnel' : 'serve', `--https=${port}`, `http://${address}`];
  serve = spawn(tailscale, args, { windowsHide: true });
  serve.stdout.resume();
  let problem = '';
  serve.stderr.setEncoding('utf8').on('data', chunk => { problem = (problem + chunk).slice(-2000); });
  serve.on('error', error => { problem = error.message; });
  for (let attempt = 0; attempt < 20 && !stopping; attempt++) {
    if (problem || serve.exitCode !== null || serve.signalCode !== null) break;
    if (Object.values((await serveStatus()).Foreground ?? {}).some(forwarded)) {
      serve.once('exit', () => {
        if (stopping) return;
        report('error', { message: 'Tailscale stopped forwarding Willow Mobile.' });
        process.exitCode = 1;
        stop();
      });
      return;
    }
    await delay(250);
  }
  throw new Error(problem.trim() || `Tailscale could not open port ${port}.`);
}

function probe(origin, address) {
  const { hostname, host } = new URL(origin);
  return new Promise(resolve => {
    // Connect by address: this PC may not resolve its own ts.net name.
    const request = https.get({ host: address, port, path: '/', servername: hostname, headers: { Host: host },
      signal: setup.signal, timeout: 30000 }, reply => {
      reply.resume();
      resolve(reply.statusCode === 200);
    });
    request.once('timeout', () => request.destroy());
    request.once('error', () => resolve(false));
  });
}

// Funnel publishes the ts.net name in public DNS; wait until the phone can look it up.
async function publicAddress(host) {
  const resolver = new Resolver({ timeout: 3000, tries: 1 });
  resolver.setServers(['1.1.1.1', '8.8.8.8']);
  report('progress', { message: 'Publishing the Willow Mobile link with Tailscale Funnel…' });
  for (let attempt = 0; attempt < 60 && !stopping; attempt++) {
    try { return (await resolver.resolve4(host))[0]; } catch {}
    await delay(3000);
  }
  if (!stopping) throw new Error('Tailscale Funnel did not publish the link. Check Funnel in the Tailscale admin console.');
}

// The first HTTPS request makes Tailscale fetch the certificate; do it before the phone scans.
async function warmCertificate(route) {
  const address = route.access === 'funnel' ? await publicAddress(route.host) : route.ip;
  if (stopping) return;
  report('progress', { message: 'Preparing the Tailscale HTTPS certificate…' });
  for (let attempt = 0; attempt < 5 && !stopping; attempt++) {
    if (await probe(route.origin, address)) return;
    await delay(2000);
  }
  if (!stopping) throw new Error('Tailscale could not serve HTTPS. Check that HTTPS certificates are enabled for your tailnet.');
}

process.once('SIGINT', stop);
process.once('SIGTERM', stop);
process.once('SIGHUP', stop);
process.stdin.on('end', stop);
process.stdin.resume();
try {
  if (!Number.isSafeInteger(pid) || pid < 1 || !tailscale || !emulatorRunning()) {
    throw new Error('Open mGBA using Launch mGBA.cmd and choose Pair iPhone.');
  }
  if (Number(process.versions.node.split('.')[0]) < 20) {
    throw new Error('Install Node.js 20 or newer, then reopen Willow.');
  }
  const status = await tailnetStatus();
  if (status.BackendState !== 'Running') throw new Error('Connect the Windows Tailscale app to your tailnet first.');
  const route = await chooseRoute(status);
  await installDependencies();
  if (!stopping) {
    const [{ default: QRCode }, { createMobileServer }] = await Promise.all([
      import('qrcode'), import('./mobile-server.mjs'),
    ]);
    const { origin } = route;
    const token = randomBytes(24).toString('base64url');
    const link = `${origin}/#pair=${token}`;
    const qr = await QRCode.toBuffer(link, { scale: 6, margin: 4, errorCorrectionLevel: 'M' });
    mobile = await createMobileServer({ origin, token, pid });
    await startForward(await listen(mobile.server), route);
    if (route.access !== 'http' && !stopping) await warmCertificate(route);
    if (!stopping) {
      report('ready', { origin, link, access: route.access, qr: qr.toString('base64') });
      monitor = setInterval(() => { if (!emulatorRunning()) stop(); }, 2000);
    }
  }
} catch (error) {
  if (!stopping) {
    report('error', { message: error.message });
    process.exitCode = 1;
  }
  stop();
} finally {
  if (stopping) stop();
}
