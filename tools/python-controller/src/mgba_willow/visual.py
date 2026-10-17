"""Advisory visual stall detection for unsupported games; stdlib PNG decoding."""

import base64
import struct
import zlib

from .policy import DIRECTIONS


def pixels(png):
    try:
        data = base64.b64decode(png, validate=True)
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        parts, header, offset = [], None, 8
        while offset + 12 <= len(data):
            length = struct.unpack_from(">I", data, offset)[0]
            if offset + length + 12 > len(data):
                return None
            kind, chunk = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + length]
            if kind == b"IHDR":
                header = chunk
            if kind == b"IDAT":
                parts.append(chunk)
            offset += length + 12
        if header is None or len(header) != 13 or not parts:
            return None
        width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", header)
        channels = {2: 3, 6: 4}.get(color, 0)
        if depth != 8 or not channels or interlace != 0 or width * height > 1024 * 1024:
            return None
        # Match Node's 4 MiB decompression cap, including PNG row filter bytes.
        inflater = zlib.decompressobj()
        raw = inflater.decompress(b"".join(parts), 4 * 1024 * 1024 + 1)
        stride = width * channels
        if len(raw) > 4 * 1024 * 1024 or not inflater.eof or len(raw) != (stride + 1) * height:
            return None
        output = bytearray(stride * height)
        for y in range(height):
            filter_kind = raw[y * (stride + 1)]
            if filter_kind > 4:
                return None
            for x in range(stride):
                index = y * stride + x
                left = output[index - channels] if x >= channels else 0
                up = output[index - stride] if y else 0
                corner = output[index - stride - channels] if y and x >= channels else 0
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                predictor = (0 if filter_kind == 0 else left if filter_kind == 1 else up if filter_kind == 2
                             else (left + up) // 2 if filter_kind == 3
                             else left if pa <= pb and pa <= pc else up if pb <= pc else corner)
                output[index] = (raw[y * (stride + 1) + x + 1] + predictor) & 255
        return width, height, channels, output
    except (ValueError, TypeError, IndexError, struct.error, zlib.error):
        return None


def stable(a, b):
    if a is None or b is None or a[:3] != b[:3]:
        return False
    width, height, channels, data = a
    changed = sum(data[i:i + 3] != b[3][i:i + 3] for i in range(0, len(data), channels))
    return changed <= int(width * height * 0.0125)


async def guarded_movement(request, method, args):
    actions = [args] if method == "act" else args["actions"]

    def directional(action):
        return len(action["buttons"]) == 1 and action["buttons"][0] in DIRECTIONS

    if not any(directional(action) and action["frames"] >= 64 for action in actions):
        return await request(method, args)
    before = await request("observe", {"screenshot": True})
    if (before.get("game_state") or {}).get("supported") is not False or pixels(before.get("png")) is None:
        return await request(method, args)
    steps, first_frame, stalled = [], None, False
    for action in actions:
        remaining, unchanged, anchor, start = action["frames"], 0, pixels(before.get("png")), None
        while remaining > 0:
            frames = min(32, remaining) if directional(action) else remaining
            result = await request("act", {"buttons": action["buttons"], "frames": frames, "screenshot": True})
            if first_frame is None:
                first_frame = result.get("action_start_frame")
            if start is None:
                start = result.get("action_start_frame")
            if result.get("cancelled") or result.get("error"):
                break
            remaining -= frames
            b = pixels(result.get("png"))
            if directional(action) and stable(anchor, b):
                unchanged += frames
            else:
                unchanged, anchor = 0, b
            before = result
            if unchanged >= 64:
                stalled = True
                break
        if not remaining and not result.get("cancelled") and not result.get("error"):
            steps.append({"action_start_frame": start, "action_end_frame": result.get("action_end_frame")})
        if stalled or result.get("cancelled") or result.get("error"):
            break
    if result.get("error"):
        return result
    result = {**result, "action_start_frame": first_frame,
              "progress": {**result.get("progress", {}), "detection": "visual_stability",
                           "completed": not stalled and not bool(result.get("cancelled")),
                           "reason": "visual_stall" if stalled else "cancelled" if result.get("cancelled") else "frames_completed",
                           "possible_obstacle": stalled, "requires_replan": stalled,
                           "detail": ("Screen stable for 64 directional frames (at most 1.25% changed pixels). Stop and inspect another route; this is visual evidence, not verified collision."
                                      if stalled else "Screen changes do not prove successful movement.")}}
    if method == "act_sequence":
        result["steps"] = steps
    if args.get("screenshot") is False:
        result.pop("png", None)
    return result
