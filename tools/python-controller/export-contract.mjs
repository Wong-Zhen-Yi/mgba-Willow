// Development-only: snapshot the Node tool contract without opening stdio or a game.
// Run after npm ci in ../mcp. Python has no Node runtime dependency.
import vm from 'node:vm';
import { readFile, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
const require = createRequire(new URL('../mcp/package.json', import.meta.url));
const { z } = require('zod');
const captured = { tools: [] };
class McpServer {
  constructor(info, options) { Object.assign(captured, { info, ...options }); }
  registerTool(name, options) {
    const schema = z.toJSONSchema(z.object(options.inputSchema), { target: 'draft-7' });
    delete schema.$schema;
    captured.tools.push({ name, description: options.description, inputSchema: schema, annotations: options.annotations });
  }
}
let source = await readFile(new URL('../mcp/server.mjs', import.meta.url), 'utf8');
source = source.replace(/^import .*;\r?\n/gm, '').replace('export function content', 'function content');
source = source.slice(0, source.indexOf('async function shutdown()'));
vm.runInNewContext(source, { McpServer, z, StateResponses: class {}, process: { env: {} } });
const output = JSON.stringify(captured, null, 2) + '\n';
const target = new URL('src/mgba_willow/contract.json', import.meta.url);
if (process.argv.includes('--check')) {
  if (await readFile(target, 'utf8') !== output) throw new Error('Python tool contract differs; run export-contract.mjs');
} else await writeFile(target, output);
