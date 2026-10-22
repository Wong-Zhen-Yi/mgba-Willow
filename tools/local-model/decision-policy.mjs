// These are model choice weights, not calibrated probabilities of gameplay success.
export const DEFAULT_NAVIGATION_PROBABILITY = 0.3;
export const DEFAULT_CONFIRMATION_PROBABILITY = 0.6;

// Only the bounded, prevalidated candidates in player.mjs can reach this gate.
export function probabilityPolicy(action, { minProbability, minNavigationProbability, candidateCount } = {}) {
  if (action.id === 'stop') return { category: 'model_stop', min_probability: 0 };
  const navigation = action.method === 'move_to' ||
    (action.id === 'wait' && action.method === 'act' && action.args.buttons.length === 0) ||
    (action.method === 'press' && ['Up', 'Down', 'Left', 'Right'].includes(action.args.button) &&
      ['battle_action', 'battle_move'].includes(action.args.expected_menu?.kind));
  return navigation
    ? { category: 'navigation', min_probability: minNavigationProbability ?? minProbability ??
      Math.min(DEFAULT_NAVIGATION_PROBABILITY, candidateCount > 1 ? 1 / candidateCount : DEFAULT_NAVIGATION_PROBABILITY) }
    : { category: 'confirmation', min_probability: minProbability ?? DEFAULT_CONFIRMATION_PROBABILITY };
}

export function stopExplanation({ reason, details = {} }) {
  const percent = value => `${(value * 100).toFixed(1)}%`;
  switch (reason) {
    case 'low_probability':
      return `The selected ${details.action} scored ${percent(details.probability)}, below the ${percent(details.min_probability)} ${details.category} threshold. No input was sent for that decision. Inspect the game or choose a more specific goal; this is a choice score, not a calibrated success rate.`;
    case 'model_stop':
      return 'Kev chose to hand control back. This does not establish goal completion. Inspect the game for unclear routes, healing, prompts or a safe save point before selecting another goal.';
    case 'needs_human_inspection':
      return `The decoded ${details.interaction ?? 'unknown'} state (menu: ${details.menu ?? 'unknown'}) has no supported autonomous action. Handle the prompt manually, then select a goal again. Unknown dialogue, move learning and evolution are not dismissed automatically.`;
    case 'unsupported_or_unavailable_state':
      return 'The ROM is unsupported or its decoded state is unavailable. Use a supported English Emerald ROM and resume manually if paused. No blind inputs were sent.';
    case 'human_interruption':
      return 'A manual pause, human input or cancelled action interrupted this run. Release the controls and resume manually before selecting another goal.';
    case 'other_agent_connected':
      return 'Another AI client is connected. Disconnect it before starting the local player again.';
    case 'unverified_confirmation':
      return 'The last confirmation did not produce a verified state change within 120 wait frames. Inspect the game; the confirmation was not repeated.';
    case 'stalled':
    case 'navigation_loop':
      return 'The player detected no progress or a repeated route. Inspect the route or configure Luna assistance instead of repeating the same input.';
    case 'model_unavailable':
      return 'Kev remained unavailable after bounded retries and no approved recovery was available. Inspect the model server and local-player log.';
    case 'step_budget':
      return 'This bounded run reached its decision budget. Inspect progress and save at a safe point before starting another run.';
    case 'time_budget':
      return 'This bounded run reached its time budget. Inspect progress and save at a safe point before starting another run.';
    case 'cancelled':
      return 'The local-player run was cancelled. The emulator is not automatically paused or saved.';
    default:
      return 'Inspect the game and local-player log before retrying; this stop does not establish goal completion.';
  }
}
