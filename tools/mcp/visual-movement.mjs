import { inflateSync } from 'node:zlib';

// Compare image data, ignoring PNG timestamps and other ancillary metadata.
// Ignore small sprite animation changes; this remains an advisory heuristic.
function pixels(png) {
  try {
    const bytes = Buffer.from(png, 'base64');
    if (bytes.subarray(0, 8).toString('hex') !== '89504e470d0a1a0a') return;
    const parts = [];
    let header;
    for (let offset = 8; offset + 12 <= bytes.length;) {
      const length = bytes.readUInt32BE(offset);
      if (offset + length + 12 > bytes.length) return;
      const type = bytes.toString('ascii', offset + 4, offset + 8);
      const data = bytes.subarray(offset + 8, offset + 8 + length);
      if (type === 'IHDR') header = data;
      if (type === 'IDAT') parts.push(data);
      offset += length + 12;
    }
    if (!header || !parts.length) return;
    const width = header.readUInt32BE(0), height = header.readUInt32BE(4);
    const channels = header[9] === 2 ? 3 : header[9] === 6 ? 4 : 0;
    if (header[8] !== 8 || !channels || header[12] !== 0 || width * height > 1024 * 1024) return;
    const raw = inflateSync(Buffer.concat(parts), { maxOutputLength: 4 * 1024 * 1024 });
    const stride = width * channels, data = Buffer.alloc(stride * height);
    if (raw.length !== (stride + 1) * height) return;
    for (let y = 0; y < height; y++) {
      const filter = raw[y * (stride + 1)];
      if (filter > 4) return;
      for (let x = 0; x < stride; x++) {
        const index = y * stride + x;
        const left = x >= channels ? data[index - channels] : 0;
        const up = y ? data[index - stride] : 0;
        const corner = y && x >= channels ? data[index - stride - channels] : 0;
        const p = left + up - corner;
        const pa = Math.abs(p - left), pb = Math.abs(p - up), pc = Math.abs(p - corner);
        const predictor = filter === 0 ? 0 : filter === 1 ? left : filter === 2 ? up : filter === 3 ? Math.floor((left + up) / 2) : pa <= pb && pa <= pc ? left : pb <= pc ? up : corner;
        data[index] = (raw[y * (stride + 1) + x + 1] + predictor) & 255;
      }
    }
    return { width, height, channels, data };
  } catch { return; }
}

function stable(a, b) {
  if (!a || !b || a.width !== b.width || a.height !== b.height || a.channels !== b.channels) return false;
  let changed = 0;
  for (let i = 0; i < a.data.length; i += a.channels) {
    if (a.data[i] !== b.data[i] || a.data[i + 1] !== b.data[i + 1] || a.data[i + 2] !== b.data[i + 2]) changed++;
  }
  return changed <= Math.floor(a.width * a.height * 0.0125);
}

export async function guardedMovement(request, method, args) {
  const actions = method === 'act' ? [args] : args.actions;
  const directional = action => action.buttons.length === 1 && ['Up', 'Down', 'Left', 'Right'].includes(action.buttons[0]);
  if (!actions.some(action => directional(action) && action.frames >= 64)) return request(method, args);
  let before = await request('observe', { screenshot: true });
  // Verified coordinates are stronger evidence than screen equality.
  if (before.game_state?.supported !== false || !pixels(before.png)) return request(method, args);
  const steps = [];
  let firstFrame, result, stalled = false;
  for (const action of actions) {
    let remaining = action.frames, unchanged = 0, anchor = pixels(before.png);
    let start;
    while (remaining > 0) {
      const frames = directional(action) ? Math.min(32, remaining) : remaining;
      result = await request('act', { buttons: action.buttons, frames, screenshot: true });
      firstFrame ??= result.action_start_frame;
      start ??= result.action_start_frame;
      if (result.cancelled || result.error) break;
      remaining -= frames;
      const b = pixels(result.png);
      if (directional(action) && stable(anchor, b)) unchanged += frames;
      else { unchanged = 0; anchor = b; }
      before = result;
      if (unchanged >= 64) { stalled = true; break; }
    }
    if (!remaining && !result.cancelled && !result.error) steps.push({ action_start_frame: start, action_end_frame: result.action_end_frame });
    if (stalled || result.cancelled || result.error) break;
  }
  if (result.error) return result;
  result = { ...result, action_start_frame: firstFrame,
    progress: { ...result.progress, detection: 'visual_stability', completed: !stalled && !result.cancelled,
      reason: stalled ? 'visual_stall' : result.cancelled ? 'cancelled' : 'frames_completed',
      possible_obstacle: stalled, requires_replan: stalled,
      detail: stalled ? 'Screen stable for 64 directional frames (at most 1.25% changed pixels). Stop and inspect another route; this is visual evidence, not verified collision.' : 'Screen changes do not prove successful movement.' } };
  if (method === 'act_sequence') result.steps = steps;
  if (args.screenshot === false) delete result.png;
  return result;
}



