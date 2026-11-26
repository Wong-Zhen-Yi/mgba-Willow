// Willow Auto Play extension for the Pi coding agent (https://pi.dev).
// Pi has no MCP client, so this starts server.mjs over stdio and registers each
// mgba-willow tool as a Pi tool. autoplay.mjs loads it with `pi -e`.
import { readFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

const serverScript = fileURLToPath(new URL('./server.mjs', import.meta.url));
export const codexProvider = 'willow-codex';
// Auto Play may only edit these progress notes (see autoPlayPrompt in autoplay.mjs).
export const noteFiles = ['CURRENT_HANDOFF.md', 'POKEMON_EMERALD_OBJECTIVES.md', 'STORY_PROGRESS_NOTES.md'];
// Matches gameActions in autoplay.mjs: tools that advance the game.
const gameActions = new Set(['press', 'act', 'act_sequence', 'move_to', 'train_battle']);

const codexModels = [
  { id: 'gpt-6-luna', name: 'GPT-6 Luna' },
  { id: 'gpt-6.1-terra', name: 'GPT-6.1 Terra' },
].map(model => ({
  ...model,
  reasoning: true,
  thinkingLevelMap: { xhigh: 'xhigh', minimal: 'low' },
  input: ['text', 'image'],
  cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0 },
  contextWindow: 272000,
  maxTokens: 128000,
}));

// The Codex CLI's ChatGPT sign-in. Read-only: Codex refreshes it, and Pi never
// uses the refresh token, so both stay signed in.
export function codexAccessToken(home = process.env.CODEX_HOME || path.join(os.homedir(), '.codex')) {
  let auth;
  try {
    auth = JSON.parse(readFileSync(path.join(home, 'auth.json'), 'utf8'));
  } catch {
    return undefined;
  }
  const token = auth?.tokens?.access_token;
  if (typeof token !== 'string') return undefined;
  try {
    const { exp } = JSON.parse(Buffer.from(token.split('.')[1], 'base64url').toString('utf8'));
    if (typeof exp === 'number' && exp * 1000 < Date.now() + 60000) return undefined;
  } catch {
    return undefined;
  }
  return token;
}

// Maps MCP tool content to Pi tool content (text and images pass through).
export function toolContent(result) {
  const content = [];
  for (const item of result?.content ?? []) {
    if (item.type === 'text') content.push({ type: 'text', text: item.text });
    else if (item.type === 'image') content.push({ type: 'image', data: item.data, mimeType: item.mimeType });
  }
  if (!content.length && result?.structuredContent) content.push({ type: 'text', text: JSON.stringify(result.structuredContent) });
  return content;
}

// Game state goes stale within seconds, but Pi resends the whole turn on every
// model call. Keep screenshots in the newest few game tool results and full text
// in the newest dozen; older ones shrink. Earlier messages never change again,
// so the provider's prompt cache still covers them.
export function trimContext(messages, tools, { keepImages = 3, keepText = 12, limit = 1200 } = {}) {
  let images = 0;
  let results = 0;
  for (let index = messages.length - 1; index >= 0; --index) {
    const message = messages[index];
    if (message.role !== 'toolResult' || !tools.has(message.toolName)) continue;
    const old = ++results > keepText;
    const hasImage = message.content.some(part => part.type === 'image');
    const dropImage = hasImage && ++images > keepImages;
    if (!old && !dropImage) continue;
    messages[index] = {
      ...message,
      content: message.content.map(part => {
        if (part.type === 'image' && dropImage) return { type: 'text', text: '[Older screenshot omitted]' };
        if (part.type === 'text' && old && part.text.length > limit)
          return { type: 'text', text: `${part.text.slice(0, limit)}… [older output trimmed]` };
        return part;
      }),
    };
  }
  return messages;
}

export const wrapUp = actions => `You have used ${actions} game actions this turn. Finish the current battle or dialogue, ` +
  'reach a safe point, update CURRENT_HANDOFF.md if you made progress, disconnect, and end with the AUTOPLAY status line.';

function insideRoot(cwd, file) {
  const relative = path.relative(cwd, path.resolve(cwd, String(file ?? '').replace(/^@/, '')));
  return relative && !relative.startsWith('..') && !path.isAbsolute(relative) ? relative : undefined;
}

// Why Auto Play refuses a built-in tool call, or undefined to allow it.
export function blockedReason(toolName, input, cwd) {
  if (toolName === 'bash') return 'Auto Play cannot run shell commands.';
  if (toolName !== 'edit' && toolName !== 'write') return undefined;
  const relative = insideRoot(cwd, input?.path);
  if (relative && noteFiles.some(name => name.toLowerCase() === relative.toLowerCase())) return undefined;
  return `Auto Play may only edit ${noteFiles.join(', ')}.`;
}

export default async function willow(pi) {
  const token = codexAccessToken();
  if (token) {
    pi.registerProvider(codexProvider, {
      name: 'Codex (Willow)',
      baseUrl: 'https://chatgpt.com/backend-api',
      api: 'openai-codex-responses',
      apiKey: token,
      models: codexModels,
    });
  }

  const env = Object.fromEntries(Object.entries(process.env).filter(([, value]) => value !== undefined));
  const client = new Client({ name: 'willow-pi', version: '1.0.0' });
  await client.connect(new StdioClientTransport({ command: process.execPath, args: [serverScript], env, stderr: 'ignore' }));
  const instructions = client.getInstructions() ?? '';
  const { tools } = await client.listTools();

  for (const tool of tools) {
    pi.registerTool({
      name: tool.name,
      label: `mgba-willow ${tool.name}`,
      description: tool.description ?? tool.name,
      promptSnippet: (tool.description ?? tool.name).split(/(?<=\.)\s/)[0],
      parameters: { type: 'object', properties: {}, ...tool.inputSchema },
      // One emulator: game inputs and observations must not overlap.
      executionMode: 'sequential',
      async execute(_id, params, signal) {
        const result = await client.callTool({ name: tool.name, arguments: params ?? {} }, undefined,
          { signal, timeout: 300000, resetTimeoutOnProgress: true });
        const content = toolContent(result);
        if (result.isError) throw new Error(content.map(item => item.text).filter(Boolean).join('\n') || `${tool.name} failed.`);
        return { content, details: result.structuredContent ?? {} };
      },
    });
  }

  pi.on('before_agent_start', event => ({
    systemPrompt: `${event.systemPrompt}\n\n# mgba-willow tools\n\n${instructions}`,
  }));

  const names = new Set(tools.map(tool => tool.name));
  pi.on('context', event => ({ messages: trimContext(event.messages, names) }));

  // Each `pi -p` run is one Auto Play turn. Ask the agent to wrap up after the
  // budget, and refuse further game input at twice the budget so notes, saves
  // and compaction happen between turns.
  const budget = Math.max(1, Number(process.env.WILLOW_PI_TURN_ACTIONS) || 60);
  let actions = 0;
  pi.on('tool_execution_end', event => {
    if (!gameActions.has(event.toolName) || event.isError) return;
    if (++actions === budget) pi.sendUserMessage(wrapUp(actions), { deliverAs: 'steer' });
  });

  pi.on('tool_call', (event, ctx) => {
    if (gameActions.has(event.toolName) && actions >= budget * 2) {
      return { block: true, reason: `${wrapUp(actions)} Further game input is refused until the next turn.` };
    }
    const reason = blockedReason(event.toolName, event.input, ctx.cwd);
    return reason ? { block: true, reason } : undefined;
  });

  pi.on('session_shutdown', () => client.close().catch(() => {}));
}
