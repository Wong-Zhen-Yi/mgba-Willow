/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include "willow/AIGameplay.h"
#include "willow/EmeraldGameState.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct mCore;

namespace Willow {

// The MCP bridge protocol that tools/mcp/bridge.mjs speaks: one JSON request
// per line, {"id", "method", "args"}, answered by {"id", "result"} or
// {"id", "error"}. Everything here is independent of the frontend, which owns
// the socket, the emulation thread lock and the pending-action state.
namespace Bridge {

constexpr int Protocol = 1;
// Longer request lines close the connection.
constexpr std::size_t MaxLineBytes = 65536;
// Actions that run longer than this are cancelled.
constexpr int ActionTimeoutMs = 30000;

// %LOCALAPPDATA%/mgba-willow/ai on Windows, ~/.local/share/mgba-willow/ai
// elsewhere: the folder tools/mcp/bridge.mjs reads session descriptors from.
std::string dataDirectory();
// The local socket name: a named pipe on Windows, a socket in the temporary
// folder elsewhere.
std::string pipeName(const std::string& sessionId);
// The contents of <dataDirectory>/sessions/<sessionId>.json.
Json sessionDescriptor(const std::string& sessionId, int64_t pid);

// A JSON number that is a whole number within [min, max].
bool isInteger(const Json& value, double min, double max);
bool isRequestId(const Json& id);

// What the "session" method reports. The frontend fills it in.
struct Session {
	std::string id;
	bool hasGame = false;
	std::string title;
	std::string gameId;
	bool gba = false;
	bool started = false;
	bool aiControl = false;
	uint64_t frame = 0;
	bool paused = false;
	bool openGL = false;
	int clients = 0;
	bool mcpEnabled = false;
	std::string objective;
	int aiSpeed = 4; // -1 is "maximum"
	unsigned humanKeys = 0;
	unsigned aiKeys = 0;
};
Json sessionJson(const Session& session);
// The phone shows the game's display name where the ROM title is cryptic.
Json mobileSessionJson(const Session& session);

// AI speed multipliers: 1, 2, 4, 8, or -1 for maximum.
bool isAISpeed(int speed);
// The set_speed argument: 1, 2, 4, 8 or "maximum".
bool parseSpeed(const Json& multiplier, int* speed);
// The phone's speed buttons: as above, plus 0 for normal speed.
bool isMobileSpeed(int speed);
unsigned keyMask(bool gba);

// act, act_sequence and press, turned into AI steps. press is a sequence of
// release, hold and release that defaults to no screenshot.
struct ActionPlan {
	std::vector<AIGameplay::Step> steps;
	std::vector<int> frames;
	bool sequence = false;
	bool screenshot = true;
	// Up/Down/Left/Right bits pressed in any step, for stall detection.
	unsigned directionKeys = 0;
};
bool parseAction(const std::string& method, const Json& args, bool gba, bool paused, ActionPlan* plan,
                 std::string* error);
// The per-step frame ranges of an act_sequence reply.
Json completedSteps(uint64_t start, const std::vector<int>& frames, std::size_t completed);

// set_objective text: a string of at most 512 UTF-16 code units.
bool isObjective(const Json& text);
// observe, get_game_state and get_local_map arguments.
bool checkObserve(const std::string& method, const Json& args, std::string* error);

// move_to and wait_until arguments, checked before the game is touched.
bool checkStateAction(const std::string& method, const Json& args, std::string* error);

// Memory tools. Only plain ROM and RAM can be read: no MMIO, BIOS, virtual
// registers, cartridge control or EEPROM. The caller holds the core paused.
Json memoryMap(mCore* core);
bool readMemory(mCore* core, const Json& args, Json* result, std::string* error);

// save_checkpoint and load_checkpoint, stored per ROM CRC32 under
// <dataDirectory>/checkpoints. The caller holds the core paused.
bool isCheckpointName(const std::string& name);
bool checkpoint(mCore* core, const std::string& method, const Json& args, std::string* error);

// Spots an agent walking into the same wall: the player's tile did not move
// during a direction action, from the same start, several times in a row.
class MovementTracker {
public:
	// Null unless the action pressed a direction and the start state was readable.
	Json progress(uintptr_t client, const Json& start, const Json& end, unsigned directionKeys);
	void forget(uintptr_t client) { m_stalls.erase(client); }
	void clear() { m_stalls.clear(); }

private:
	struct Stall {
		std::string signature;
		int count = 0;
	};
	std::unordered_map<uintptr_t, Stall> m_stalls;
};

} // namespace Bridge
} // namespace Willow
