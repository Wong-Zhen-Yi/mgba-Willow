// Willow Auto Play: runs Codex or Pi turns against one mGBA window until stdin closes.
// Started by the Qt app (AI → Auto Play). Reports JSON lines on stdout.
import { execFile, spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { existsSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { Bridge, descriptors, sessionDirectory } from './bridge.mjs';

// harness: which agent CLI runs the turns. Pi models are "<provider>/<id>"; the
// willow-codex provider (pi-extension.mjs) reuses the Codex CLI's sign-in.
export const profiles = {
  terra: { harness: 'codex', model: 'gpt-6.1-terra', effort: 'medium', label: 'GPT-6.1 Terra · medium' },
  luna: { harness: 'codex', model: 'gpt-6-luna', effort: 'xhigh', label: 'GPT-6 Luna · extra high' },
  'pi-terra': { harness: 'pi', model: 'willow-codex/gpt-6.1-terra', effort: 'medium', label: 'Pi · GPT-6.1 Terra · medium' },
  'pi-luna': { harness: 'pi', model: 'willow-codex/gpt-6-luna', effort: 'xhigh', label: 'Pi · GPT-6 Luna · extra high' },
};

const repository = path.resolve(fileURLToPath(new URL('../..', import.meta.url)));
const serverScript = fileURLToPath(new URL('./server.mjs', import.meta.url));
const piExtension = fileURLToPath(new URL('./pi-extension.mjs', import.meta.url));
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

// Pi runs one print-mode turn per process and keeps the conversation in its own
// session file, so later turns pass the same --session-id. Bash is off; the
// extension also limits edits to the progress notes.
export function piArguments({ profile, sessionId, session, resume, sessionDir }) {
  return [
    '--mode', 'json', '-p',
    '--no-extensions', '-e', piExtension,
    '--no-skills', '--no-prompt-templates', '--no-themes',
    '--exclude-tools', 'bash',
    '--session-dir', sessionDir,
    '--session-id', session,
    '--model', profile.model,
    '--thinking', profile.effort,
    resume ? continuePrompt(sessionId) : autoPlayPrompt(sessionId),
  ];
}

// Finds Pi's cli.js beside an npm shim (pi.cmd) so it runs under this Node without a shell.
export function piCommand(program) {
  if (/\.[cm]?js$/i.test(program)) return { command: process.execPath, prefix: [program] };
  const directory = path.dirname(program);
  for (const scope of ['@earendil-works', '@mariozechner']) {
    const cli = path.join(directory, 'node_modules', scope, 'pi-coding-agent', 'dist', 'cli.js');
    if (existsSync(cli)) return { command: process.execPath, prefix: [cli] };
  }
  return { command: program, prefix: [] };
}

// Short activity text for one Codex or Pi JSON event, or undefined for events not worth showing.
export function describe(event) {
  const item = event?.item;
  if (event?.type === 'item.started' && item?.type === 'mcp_tool_call' && item.server === 'mgba-willow')
    return `Using ${item.tool}`;
  if (event?.type === 'tool_execution_start' && event.toolName) return `Using ${event.toolName}`;
  if (event?.type === 'auto_retry_start') return `Retrying the model request (${event.attempt}/${event.maxAttempts})`;
  if (event?.type === 'compaction_start') return 'Compacting the conversation';
  return undefined;
}

const messageText = message => (message?.content ?? [])
  .filter(part => part.type === 'text').map(part => part.text).join('\n');

// Folds one Codex `exec --json` event into the turn result.
export function readCodexEvent(event, result) {
  const item = event.item;
  if (event.type === 'thread.started') result.thread = event.thread_id;
  if (event.type === 'item.completed' && item?.type === 'mcp_tool_call' &&
      item.server === 'mgba-willow' && gameActions.has(item.tool) && item.status === 'completed' && !item.error) {
    result.tools++;
  }
  if (event.type === 'item.completed' && item?.type === 'agent_message') result.message = item.text ?? '';
  if (event.type === 'error' || event.type === 'turn.failed') {
    result.error = event.message ?? event.error?.message ?? 'Codex reported an error.';
  }
}

// Folds one Pi `--mode json` event into the turn result. Pi exits 0 even when the
// model request fails, so errors come from the last assistant message.
export function readPiEvent(event, result) {
  if (event.type === 'session') result.thread = event.id;
  if (event.type === 'tool_execution_end' && gameActions.has(event.toolName) && !event.isError) result.tools++;
  if (event.type === 'message_end' && event.message?.role === 'assistant') {
    const { stopReason, errorMessage } = event.message;
    const text = messageText(event.message);
    if (text) result.message = text;
    result.error = stopReason === 'error' || stopReason === 'aborted'
      ? errorMessage || `Pi stopped (${stopReason}).` : '';
    result.tokens += event.message.usage?.totalTokens ?? 0;
  }
  if (event.type === 'auto_retry_end' && !event.success && event.finalError) result.error = event.finalError;
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

// Runs one agent turn. Resolves with the thread id, the final message and tool-call count.
function runTurn({ command, prefix, args, cwd, env, read, name, report, signal, activityTimeout }) {
  return new Promise(resolve => {
    const child = spawn(command, [...prefix, ...args], { cwd, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    const result = { thread: undefined, message: '', tools: 0, tokens: 0, error: '', code: null };
    let buffer = '';
    let stderr = '';
    const abort = () => killTree(child);
    const watchdog = setTimeout(() => {
      result.error = `${name} stopped responding during Auto Play.`;
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
        read(event, result);
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
        result.error = stderr.trim().split('\n').filter(Boolean).pop() || `${name} exited with code ${code}.`;
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

// Errors that retrying cannot fix: sign-in, quota, missing model or tools.
const fatalError = /usage limit|rate limit|quota|not logged in|log in again|unauthorized|no api key|required MCP|model.*not (?:found|supported|available)/i;

// How one harness starts and reads its turns. Codex resumes its thread id; Pi
// reuses one session id until a turn fails.
function harnessFor({ profile, program, prefix, sessionId, directory, piSessionDir }) {
  if (profile.harness === 'pi') {
    const resolved = prefix ? { command: program, prefix } : piCommand(program);
    let session = randomUUID();
    let resume = false;
    return {
      name: 'Pi', read: readPiEvent, ...resolved,
      env: { ...process.env, MGBA_WILLOW_SESSION_DIR: directory, WILLOW_AUTOPLAY_SESSION_ID: sessionId },
      args: () => piArguments({ profile, sessionId, session, resume, sessionDir: piSessionDir }),
      finished: () => { resume = true; },
      // A dropped model connection leaves the session file intact, so the first
      // retry continues it; a second failure in a row starts over.
      reset: failures => {
        const saved = failures < 2 && existsSync(piSessionDir) &&
          readdirSync(piSessionDir).some(name => name.endsWith(`_${session}.jsonl`));
        if (saved) resume = true;
        else { session = randomUUID(); resume = false; }
      },
      explain: error => /no api key|willow-codex/i.test(error)
        ? `Pi could not use your Codex sign-in (${error}). Open Codex, sign in, then start Auto Play again.` : error,
    };
  }
  let thread;
  return {
    name: 'Codex', read: readCodexEvent, command: program, prefix: prefix ?? [], env: undefined,
    args: () => codexArguments({ profile, sessionId, thread, directory }),
    finished: turn => { thread = turn.thread ?? thread; },
    reset: () => { thread = undefined; },
    explain: error => error,
  };
}

export async function runAutoPlay({
  program, prefix, profile, sessionId, cwd = repository, report, signal, directory = sessionDirectory,
  piSessionDir = path.join(path.dirname(directory), 'pi-sessions'), activityTimeout = 120000,
  state = () => sessionState(sessionId, directory), pauseInterval = 2000, retryDelay = 10000, turnDelay = 1000,
}) {
  const harness = harnessFor({ profile, program, prefix, sessionId, directory, piSessionDir });
  const { name } = harness;
  let failures = 0;
  let idle = 0;
  let turns = 0;
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
    const turn = await runTurn({
      command: harness.command, prefix: harness.prefix, args: harness.args(), cwd, env: harness.env,
      read: harness.read, name, report, signal, activityTimeout,
    });
    if (signal.aborted) break;
    if (turn.error) {
      if (fatalError.test(turn.error)) return report('error', { message: harness.explain(turn.error) });
      if (++failures >= 3) return report('error', { message: `${name} failed three times in a row: ${turn.error}` });
      report('status', { message: `${name} stopped unexpectedly (${turn.error}). Retrying…` });
      harness.reset(failures);
      await delay(retryDelay, undefined, { signal }).catch(() => {});
      continue;
    }
    failures = 0;
    harness.finished(turn);
    const { status, reason } = turnStatus(turn.message);
    if (status === 'blocked') return report('error', { message: `Auto Play needs you: ${reason || `${name} could not continue.`}` });
    // Guard usage: stop if the agent keeps ending turns without touching the game.
    idle = turn.tools === 0 && status !== 'paused' ? idle + 1 : 0;
    if (idle >= 2) return report('error', { message: `${name} ended two turns without playing. Auto Play stopped.` });
    const usage = turn.tokens ? `, ${Math.round(turn.tokens / 1000)}k tokens` : '';
    report('activity', { message: `Turn ${++turns} done (${turn.tools} game actions${usage})`, turn: turns, tools: turn.tools });
    await delay(turnDelay, undefined, { signal }).catch(() => {});
  }
}

async function main() {
  const [sessionId, program, profileName] = process.argv.slice(2);
  const profile = Object.hasOwn(profiles, profileName) ? profiles[profileName] : profiles.terra;
  const report = (type, fields) => process.stdout.write(JSON.stringify({ type, ...fields }) + '\n');
  const controller = new AbortController();
  const stop = () => controller.abort();
  process.stdin.on('end', stop);
  process.stdin.on('close', stop);
  process.stdin.resume();
  for (const name of ['SIGINT', 'SIGTERM', 'SIGHUP']) process.once(name, stop);
  if (!/^[0-9a-f-]{36}$/.test(sessionId ?? '') || !program) {
    report('error', { message: 'Start Auto Play from the AI menu in mGBA Willow.' });
    process.exit(1);
  }
  try {
    await runAutoPlay({ program, profile, sessionId, report, signal: controller.signal });
  } catch (error) {
    report('error', { message: error.message });
  }
  report('stopped', {});
  process.exit(0);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) main();
