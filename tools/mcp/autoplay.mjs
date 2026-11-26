// Willow Auto Play: runs Codex turns against one mGBA window until stdin closes.
// Started by the Qt app (AI → Auto Play). Reports JSON lines on stdout.
import { execFile, spawn } from 'node:child_process';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { Bridge, descriptors, sessionDirectory } from './bridge.mjs';

export const profiles = {
  terra: { model: 'gpt-6.1-terra', effort: 'medium', label: 'GPT-6.1 Terra · medium' },
  luna: { model: 'gpt-6-luna', effort: 'xhigh', label: 'GPT-6 Luna · extra high' },
};

const repository = path.resolve(fileURLToPath(new URL('../..', import.meta.url)));
const serverScript = fileURLToPath(new URL('./server.mjs', import.meta.url));
const gameActions = new Set(['press', 'act', 'act_sequence', 'move_to', 'train_battle']);

export function autoPlayPrompt(sessionId) {
  return `You are Willow Auto Play. Play the game open in mGBA Willow by yourself. Nobody reads or answers this chat, so never ask questions or wait for a reply.

- Begin by calling the mgba-willow connect tool with session_id "${sessionId}". The MCP server is already scoped to that window. If you cannot connect, report AUTOPLAY: blocked with the connection error.
- Each turn starts with a fresh MCP connection, so connect again before acting.
- Follow AGENTS.md and doc/willow-control.md for control, verification, in-game saves and progress notes. For Pokémon Emerald, read CURRENT_HANDOFF.md first.
- Make steady story progress toward the current objective. Keep going through battles, dialogue and travel for a long stretch (roughly 30 to 60 tool calls) before ending the turn.
- Only edit the progress notes those documents name: CURRENT_HANDOFF.md, POKEMON_EMERALD_OBJECTIVES.md and STORY_PROGRESS_NOTES.md. Don't change code, builds, settings or other files, and don't run unrelated commands.
- Don't load checkpoints, reset the game or change AI speed unless the documents call for it to recover from a mistake.
- Before ending a turn, update CURRENT_HANDOFF.md if you made progress, then disconnect.
- End every turn with exactly one final line: "AUTOPLAY: continue" to keep playing, "AUTOPLAY: paused" if the game is paused, or "AUTOPLAY: blocked <reason>" if you can't continue without the player.`;
}

export function continuePrompt(sessionId) {
  return `Continue Willow Auto Play from the live game state with the same rules. Connect to session_id "${sessionId}" again before acting, and end with the AUTOPLAY status line.`;
}

export function codexArguments({ profile, sessionId, thread, directory = sessionDirectory }) {
  const mcpConfig = [
    `command=${JSON.stringify(process.execPath)}`,
    `args=[${JSON.stringify(serverScript)}]`,
    `env={MGBA_WILLOW_SESSION_DIR=${JSON.stringify(directory)},WILLOW_AUTOPLAY_SESSION_ID=${JSON.stringify(sessionId)}}`,
    'required=true',
  ].join(',');
  const settings = [
    '-m', profile.model,
    '--ignore-user-config',
    '-c', `mcp_servers.mgba-willow={${mcpConfig}}`,
    '-c', 'windows.sandbox="unelevated"',
    '-c', `model_reasoning_effort="${profile.effort}"`,
    '-c', 'approval_policy="never"',
    '-c', 'sandbox_mode="workspace-write"',
  ];
  return thread
    ? ['exec', 'resume', '--json', ...settings, thread, continuePrompt(sessionId)]
    : ['exec', '--json', ...settings, autoPlayPrompt(sessionId)];
}

// Short activity text for one Codex JSON event, or undefined for events not worth showing.
export function describe(event) {
  const item = event?.item;
  if (event?.type === 'item.started' && item?.type === 'mcp_tool_call' && item.server === 'mgba-willow')
    return `Using ${item.tool}`;
  return undefined;
}

export function turnStatus(text) {
  const match = /^AUTOPLAY:\s*(continue|paused|blocked)\b[ \t:—-]*(.*)$/im.exec(text ?? '');
  return match ? { status: match[1].toLowerCase(), reason: match[2].trim() } : { status: 'continue', reason: '' };
}

function killTree(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  if (process.platform === 'win32') {
    // Codex starts MCP servers as children; end them with it.
    execFile('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true }, () => {});
  } else {
    child.kill('SIGTERM');
  }
}

// Runs one Codex turn. Resolves with the thread id, the final message and tool-call count.
function runTurn({ codex, codexPrefix, args, cwd, report, signal, activityTimeout }) {
  return new Promise(resolve => {
    const child = spawn(codex, [...codexPrefix, ...args], { cwd, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    const result = { thread: undefined, message: '', tools: 0, error: '', code: null };
    let buffer = '';
    let stderr = '';
    const abort = () => killTree(child);
    const watchdog = setTimeout(() => {
      result.error = 'Codex stopped responding during Auto Play.';
      abort();
    }, activityTimeout);
    signal.addEventListener('abort', abort, { once: true });
    child.stdout.setEncoding('utf8').on('data', chunk => {
      buffer += chunk;
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, end).trim();
        buffer = buffer.slice(end + 1);
        if (!line.startsWith('{')) continue;
        let event;
        try { event = JSON.parse(line); } catch { continue; }
        watchdog.refresh();
        if (event.type === 'thread.started') result.thread = event.thread_id;
        const item = event.item;
        if (event.type === 'item.completed' && item?.type === 'mcp_tool_call' &&
            item.server === 'mgba-willow' && gameActions.has(item.tool) && item.status === 'completed' && !item.error) {
          result.tools++;
        }
        if (event.type === 'item.completed' && event.item?.type === 'agent_message') result.message = event.item.text ?? '';
        if (event.type === 'error' || event.type === 'turn.failed') {
          result.error = event.message ?? event.error?.message ?? 'Codex reported an error.';
        }
        const activity = describe(event);
        if (activity) report('activity', { message: activity });
      }
    });
    child.stderr.setEncoding('utf8').on('data', chunk => { stderr = (stderr + chunk).slice(-4000); });
    child.on('error', error => { result.error = error.message; });
    child.on('close', code => {
      clearTimeout(watchdog);
      signal.removeEventListener('abort', abort);
      result.code = code;
      if (code !== 0 && !result.error) {
        result.error = stderr.trim().split('\n').filter(Boolean).pop() || `Codex exited with code ${code}.`;
      }
      resolve(result);
    });
  });
}

async function sessionState(sessionId, directory) {
  const descriptor = (await descriptors(directory)).find(item => item.session_id === sessionId);
  if (!descriptor) return null;
  let client;
  try {
    client = await Bridge.open(descriptor);
    client.timeout = 3000;
    return await client.request('session');
  } catch {
    return null;
  } finally {
    client?.close();
  }
}

export async function runAutoPlay({
  codex, codexPrefix = [], profile, sessionId, cwd = repository, report, signal, directory = sessionDirectory,
  activityTimeout = 120000,
  state = () => sessionState(sessionId, directory), pauseInterval = 2000, retryDelay = 10000, turnDelay = 1000,
}) {
  let thread;
  let failures = 0;
  let idle = 0;
  report('status', { message: `Auto Play is starting with ${profile.label}…` });
  while (!signal.aborted) {
    const session = await state();
    if (signal.aborted) break;
    if (!session || !session.started) return report('error', { message: 'Open a single-player game, then start Auto Play again.' });
    if (session.paused) {
      report('status', { message: 'Auto Play is waiting. Resume the game to continue.', paused: true });
      while (!signal.aborted && (await state())?.paused) await delay(pauseInterval, undefined, { signal }).catch(() => {});
      continue;
    }
    report('status', { message: `Auto Play is running with ${profile.label}.` });
    const args = codexArguments({ profile, sessionId, thread, directory });
    const turn = await runTurn({ codex, codexPrefix, args, cwd, report, signal, activityTimeout });
    if (signal.aborted) break;
    if (turn.error) {
      if (/usage limit|rate limit|quota|not logged in|log in again|unauthorized|required MCP|model.*not (?:found|supported|available)/i.test(turn.error)) {
        return report('error', { message: turn.error });
      }
      if (++failures >= 3) return report('error', { message: `Codex failed three times in a row: ${turn.error}` });
      report('status', { message: `Codex stopped unexpectedly (${turn.error}). Retrying…` });
      thread = undefined;
      await delay(retryDelay, undefined, { signal }).catch(() => {});
      continue;
    }
    failures = 0;
    thread = turn.thread ?? thread;
    const { status, reason } = turnStatus(turn.message);
    if (status === 'blocked') return report('error', { message: `Auto Play needs you: ${reason || 'Codex could not continue.'}` });
    // Guard usage: stop if Codex keeps ending turns without touching the game.
    idle = turn.tools === 0 && status !== 'paused' ? idle + 1 : 0;
    if (idle >= 2) return report('error', { message: 'Codex ended two turns without playing. Auto Play stopped.' });
    await delay(turnDelay, undefined, { signal }).catch(() => {});
  }
}

async function main() {
  const [sessionId, codex, profileName] = process.argv.slice(2);
  const profile = Object.hasOwn(profiles, profileName) ? profiles[profileName] : profiles.terra;
  const report = (type, fields) => process.stdout.write(JSON.stringify({ type, ...fields }) + '\n');
  const controller = new AbortController();
  const stop = () => controller.abort();
  process.stdin.on('end', stop);
  process.stdin.on('close', stop);
  process.stdin.resume();
  for (const name of ['SIGINT', 'SIGTERM', 'SIGHUP']) process.once(name, stop);
  if (!/^[0-9a-f-]{36}$/.test(sessionId ?? '') || !codex) {
    report('error', { message: 'Start Auto Play from the AI menu in mGBA Willow.' });
    process.exit(1);
  }
  try {
    await runAutoPlay({ codex, profile, sessionId, report, signal: controller.signal });
  } catch (error) {
    report('error', { message: error.message });
  }
  report('stopped', {});
  process.exit(0);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) main();
