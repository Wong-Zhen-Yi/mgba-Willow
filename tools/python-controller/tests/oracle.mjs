// Run the existing Node implementation on the same synthetic fixtures as Python.
import { StateResponses, compactState } from '../../mcp/state-responses.mjs';
import { inputProblem, guardedVerifiedInput } from '../../mcp/control-policy.mjs';
import { guardedMovement } from '../../mcp/visual-movement.mjs';
import { trainBattle } from '../../mcp/training.mjs';
import { handoff } from '../../mcp/handoff.mjs';
let input = '';
for await (const chunk of process.stdin) input += chunk;
const outputs = [];
for (const fixture of JSON.parse(input)) {
  const calls = [];
  let cursor = 0;
  const request = async (method, args) => {
    calls.push({ method, args });
    if (cursor >= fixture.responses.length) throw new Error('Fixture responses exhausted');
    return structuredClone(fixture.responses[cursor++]);
  };
  try {
    let result;
    if (fixture.kind === 'state') {
      const state = new StateResponses();
      result = fixture.responses.map(response => state.apply(response, fixture.full, fixture.compact));
    } else if (fixture.kind === 'handoff') result = handoff(fixture.state);
    else if (fixture.kind === 'compact') result = compactState(fixture.state);
    else if (fixture.kind === 'input') result = inputProblem(fixture.state, fixture.method, fixture.args) ?? null;
    else if (fixture.kind === 'verified') result = await guardedVerifiedInput(request, fixture.method, fixture.args, fixture.before);
    else if (fixture.kind === 'visual') result = await guardedMovement(request, fixture.method, fixture.args);
    else if (fixture.kind === 'training') result = await trainBattle(request, fixture.args);
    outputs.push({ result, calls });
  } catch (error) { outputs.push({ error: error.message, calls }); }
}
process.stdout.write(JSON.stringify(outputs));
