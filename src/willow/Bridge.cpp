/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "willow/Bridge.h"

#include <mgba-util/vfs.h>
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

using namespace Willow;

namespace {

const Json& field(const Json& object, const char* key) {
	static const Json missing;
	if (!object.is_object())
		return missing;
	const auto found = object.find(key);
	return found == object.end() ? missing : *found;
}

bool has(const Json& object, const char* key) {
	return object.is_object() && object.contains(key);
}

// QJsonValue::toInt: whole numbers that fit an int, otherwise the default.
int toInt(const Json& value, int fallback = 0) {
	if (!value.is_number())
		return fallback;
	const double d = value.get<double>();
	if (d != std::floor(d) || d < -2147483648.0 || d > 2147483647.0)
		return fallback;
	return int(d);
}

bool toBool(const Json& value, bool fallback = false) {
	return value.is_boolean() ? value.get<bool>() : fallback;
}

std::string toString(const Json& value) {
	return value.is_string() ? value.get<std::string>() : std::string();
}

// Length in UTF-16 code units, which is how the Qt bridge counted text.
std::size_t utf16Length(const std::string& text) {
	std::size_t length = 0;
	for (std::size_t i = 0; i < text.size(); ++i) {
		const auto byte = static_cast<unsigned char>(text[i]);
		if ((byte & 0xC0) == 0x80)
			continue;
		length += byte >= 0xF0 ? 2 : 1;
	}
	return length;
}

std::string environment(const char* name) {
#ifdef _WIN32
	std::wstring wide(name, name + std::char_traits<char>::length(name));
	const wchar_t* value = _wgetenv(wide.c_str());
	if (!value || !*value)
		return { };
	const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
	std::string utf8(size_t(std::max(size, 1)), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8.data(), size, nullptr, nullptr);
	utf8.resize(size_t(std::max(size - 1, 0)));
	return utf8;
#else
	const char* value = std::getenv(name);
	return value ? value : "";
#endif
}

// os.homedir() in Node, which tools/mcp/bridge.mjs uses.
std::string homeDirectory() {
#ifdef _WIN32
	return environment("USERPROFILE");
#else
	std::string home = environment("HOME");
	if (home.empty()) {
		if (const passwd* entry = getpwuid(getuid()))
			home = entry->pw_dir;
	}
	return home;
#endif
}

// Paths are UTF-8 everywhere in mGBA, including on Windows.
std::filesystem::path fromUtf8(const std::string& path) {
#ifdef __cpp_char8_t
	return std::filesystem::path(reinterpret_cast<const char8_t*>(path.data()),
	                             reinterpret_cast<const char8_t*>(path.data() + path.size()));
#else
	return std::filesystem::u8path(path);
#endif
}

bool makeDirectories(const std::string& path) {
	std::error_code error;
	const auto native = fromUtf8(path);
	std::filesystem::create_directories(native, error);
	return std::filesystem::is_directory(native, error);
}

// Writes through a temporary file so a crash never leaves half a checkpoint.
bool writeAtomically(const std::string& path, const std::vector<uint8_t>& bytes) {
	const std::string temporary = path + ".tmp";
	VFile* file = VFileOpen(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
	if (!file)
		return false;
	const bool written = file->write(file, bytes.data(), bytes.size()) == ssize_t(bytes.size());
	const bool synced = written && file->sync(file, nullptr, 0);
	file->close(file);
	bool renamed = false;
	if (synced) {
#ifdef _WIN32
		renamed = MoveFileExW(fromUtf8(temporary).c_str(), fromUtf8(path).c_str(),
		                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
		renamed = std::rename(temporary.c_str(), path.c_str()) == 0;
#endif
	}
	if (!renamed) {
		std::error_code error;
		std::filesystem::remove(fromUtf8(temporary), error);
	}
	return renamed;
}

bool readable(const mCoreMemoryBlock& block) {
	static const char* const safe[] = { "cart0", "cart1", "cart2", "wram", "iwram", "vram", "palette", "oam", "hram" };
	if (!(block.flags & mCORE_MEMORY_READ) || (block.flags & mCORE_MEMORY_VIRTUAL) || !block.internalName)
		return false;
	const std::string name = block.internalName;
	return std::any_of(std::begin(safe), std::end(safe), [&](const char* candidate) { return name == candidate; });
}

const char* const ButtonNames[] = { "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L" };

} // namespace

std::string Bridge::dataDirectory() {
	std::string root = environment("LOCALAPPDATA");
	if (root.empty())
		root = homeDirectory() + "/.local/share";
	return root + "/mgba-willow/ai";
}

std::string Bridge::pipeName(const std::string& sessionId) {
	return "mgba-willow-ai-" + sessionId;
}

Json Bridge::sessionDescriptor(const std::string& sessionId, int64_t pid) {
	return { { "session_id", sessionId }, { "pipe", pipeName(sessionId) }, { "pid", pid }, { "protocol", Protocol } };
}

bool Bridge::isInteger(const Json& value, double min, double max) {
	if (!value.is_number())
		return false;
	const double d = value.get<double>();
	return std::isfinite(d) && d == std::floor(d) && d >= min && d <= max;
}

bool Bridge::isRequestId(const Json& id) {
	return isInteger(id, 1, 9007199254740991.0);
}

Json Bridge::sessionJson(const Session& session) {
	const bool game = session.hasGame;
	Json speed = session.aiSpeed == -1 ? Json("maximum") : Json(session.aiSpeed);
	return { { "session_id", session.id },
	         { "title", game ? session.title : std::string() },
	         { "game_id", game ? session.gameId : std::string() },
	         { "platform", game ? (session.gba ? "GBA" : "GB/GBC") : "none" },
	         { "started", game && session.started },
	         { "ai_control", game && session.aiControl },
	         { "frame", game ? session.frame : 0 },
	         { "paused", game && session.paused },
	         { "renderer", game && session.openGL ? "opengl" : "software" },
	         { "connected", session.clients > 0 },
	         { "connected_clients", session.clients },
	         { "mcp_enabled", session.mcpEnabled },
	         { "objective", session.objective },
	         { "ai_speed", speed },
	         { "human_keys", game ? session.humanKeys : 0 },
	         { "ai_keys", game ? session.aiKeys : 0 },
	         { "effective_keys", game ? AIGameplay::mergeKeys(session.humanKeys, session.aiKeys) : 0 } };
}

Json Bridge::mobileSessionJson(const Session& session) {
	Json result = sessionJson(session);
	if (result["platform"] == "GBA" && result["title"] == "POKEMON EMER")
		result["title"] = "Pok\xC3\xA9mon Emerald";
	return result;
}

bool Bridge::isAISpeed(int speed) {
	return speed == 1 || speed == 2 || speed == 4 || speed == 8 || speed == -1;
}

bool Bridge::parseSpeed(const Json& multiplier, int* speed) {
	if (multiplier == "maximum") {
		*speed = -1;
		return true;
	}
	if (!isInteger(multiplier, 1, 8))
		return false;
	*speed = toInt(multiplier);
	return *speed != -1 && isAISpeed(*speed);
}

bool Bridge::isMobileSpeed(int speed) {
	return speed == 0 || isAISpeed(speed);
}

unsigned Bridge::keyMask(bool gba) {
	return gba ? 0x3FF : 0xFF;
}

bool Bridge::parseAction(const std::string& method, const Json& requestArgs, bool gba, bool paused, ActionPlan* plan,
                         std::string* error) {
	auto fail = [&](const char* message) {
		*error = message;
		return false;
	};
	Json args = requestArgs.is_object() ? requestArgs : Json::object();
	bool sequence = method == "act_sequence";
	if (method == "press") {
		if (!field(args, "button").is_string() ||
		    (has(args, "hold_frames") && !isInteger(args["hold_frames"], 1, 60)) ||
		    (has(args, "release_frames") && !isInteger(args["release_frames"], 1, 60)))
			return fail("Expected button, hold_frames and release_frames (1\xE2\x80\x93"
			            "60)");
		args["actions"] = Json::array({
		    { { "buttons", Json::array() }, { "frames", 2 } },
		    { { "buttons", Json::array({ args["button"] }) }, { "frames", toInt(field(args, "hold_frames"), 2) } },
		    { { "buttons", Json::array() }, { "frames", toInt(field(args, "release_frames"), 2) } },
		});
		if (!has(args, "screenshot"))
			args["screenshot"] = false;
		sequence = true;
	} else if (method != "act" && !sequence) {
		return fail("Unknown method");
	}
	if (paused)
		return fail("Game paused; resume manually before acting");
	if (has(args, "screenshot") && !args["screenshot"].is_boolean())
		return fail("Expected boolean screenshot");
	Json actions = Json::array();
	if (method == "act")
		actions.push_back(args);
	else if (field(args, "actions").is_array())
		actions = args["actions"];
	if (actions.empty() || actions.size() > AIGameplay::MaxSteps)
		return fail("Expected 1\xE2\x80\x93"
		            "200 actions");
	ActionPlan result;
	int total = 0;
	for (const auto& action : actions) {
		const Json& buttons = field(action, "buttons");
		if (!action.is_object() || !isInteger(field(action, "frames"), 1, 600) || !buttons.is_array() ||
		    buttons.size() > 10)
			return fail("Expected buttons array and 1\xE2\x80\x93"
			            "600 frames");
		unsigned keys = 0;
		for (const auto& button : buttons) {
			const std::string name = toString(button);
			const auto found = std::find_if(std::begin(ButtonNames), std::end(ButtonNames),
			                                [&](const char* candidate) { return name == candidate; });
			if (!button.is_string() || found == std::end(ButtonNames))
				return fail("Unknown button");
			const auto index = unsigned(found - std::begin(ButtonNames));
			if (!gba && index >= 8)
				return fail("L/R are only available on GBA");
			keys |= 1U << index;
		}
		const int frames = toInt(action["frames"]);
		total += frames;
		result.frames.push_back(frames);
		result.steps.push_back({ keys, frames });
		result.directionKeys |= keys & 0xF0;
	}
	if (total > 600)
		return fail("Sequence exceeds 600 total frames");
	result.sequence = sequence;
	result.screenshot = toBool(field(args, "screenshot"), true);
	*plan = std::move(result);
	return true;
}

Json Bridge::completedSteps(uint64_t start, const std::vector<int>& frames, std::size_t completed) {
	Json steps = Json::array();
	uint64_t frame = start;
	for (std::size_t i = 0; i < completed && i < frames.size(); ++i) {
		steps.push_back({ { "action_start_frame", frame }, { "action_end_frame", frame + frames[i] } });
		frame += frames[i];
	}
	return steps;
}

bool Bridge::isObjective(const Json& text) {
	return text.is_string() && utf16Length(text.get<std::string>()) <= 512;
}

bool Bridge::checkObserve(const std::string& method, const Json& args, std::string* error) {
	if (has(args, "screenshot") && !args["screenshot"].is_boolean()) {
		*error = "Expected boolean screenshot";
		return false;
	}
	if (method == "get_local_map" && has(args, "radius") && !isInteger(args["radius"], 1, 16)) {
		*error = "Expected radius 1\xE2\x80\x93"
		         "16";
		return false;
	}
	return true;
}

bool Bridge::checkStateAction(const std::string& method, const Json& args, std::string* error) {
	auto fail = [&](const char* message) {
		*error = message;
		return false;
	};
	if ((has(args, "max_frames") && !isInteger(args["max_frames"], 1, 600)) ||
	    (has(args, "screenshot") && !args["screenshot"].is_boolean()))
		return fail("Expected 1\xE2\x80\x93"
		            "600 max_frames and boolean screenshot");
	if (method == "move_to" && (!isInteger(field(args, "x"), 0, 511) || !isInteger(field(args, "y"), 0, 511)))
		return fail("Expected map coordinates x/y 0\xE2\x80\x93"
		            "511");
	if (method == "move_to" && has(args, "avoid_trainers") && !args["avoid_trainers"].is_boolean())
		return fail("Expected boolean avoid_trainers");
	static const char* const conditions[] = { "battle_menu_ready", "battle_move_ready",       "battle_target_ready",
	                                          "overworld_ready",   "map_transition_complete", "dialogue" };
	const std::string condition = toString(field(args, "condition"));
	const bool known = std::any_of(std::begin(conditions), std::end(conditions),
	                               [&](const char* candidate) { return condition == candidate; });
	if (method == "wait_until" && (!known || (has(args, "stable_frames") && !isInteger(args["stable_frames"], 1, 60))))
		return fail("Unsupported condition or stable_frames (1\xE2\x80\x93"
		            "60)");
	return true;
}

Json Bridge::memoryMap(mCore* core) {
	const mCoreMemoryBlock* blocks = nullptr;
	const size_t count = core->listMemoryBlocks(core, &blocks);
	Json regions = Json::array();
	for (size_t i = 0; i < count; ++i) {
		const auto& block = blocks[i];
		regions.push_back({ { "name", block.internalName ? block.internalName : "" },
		                    { "description", block.longName ? block.longName : "" },
		                    { "start", block.start },
		                    { "end", block.end },
		                    { "size", block.size },
		                    { "max_segment", block.maxSegment },
		                    { "segment_start", block.segmentStart },
		                    { "readable", readable(block) } });
	}
	return { { "regions", regions } };
}

bool Bridge::readMemory(mCore* core, const Json& args, Json* result, std::string* error) {
	if (!isInteger(field(args, "address"), 0, 4294967295.0) || !isInteger(field(args, "length"), 1, 4096) ||
	    (has(args, "segment") && !isInteger(args["segment"], -1, 65535))) {
		*error = "Invalid address, length, or segment";
		return false;
	}
	const auto address = uint32_t(args["address"].get<double>());
	const int length = toInt(args["length"]);
	const int segment = toInt(field(args, "segment"), -1);
	const mCoreMemoryBlock* blocks = nullptr;
	const size_t count = core->listMemoryBlocks(core, &blocks);
	const mCoreMemoryBlock* region = nullptr;
	for (size_t i = 0; i < count; ++i) {
		if (readable(blocks[i]) && address >= blocks[i].start && uint64_t(address) + length <= blocks[i].end) {
			region = &blocks[i];
			break;
		}
	}
	if (!region || !region->size ||
	    (segment >= 0 && (segment > region->maxSegment || address < region->segmentStart))) {
		*error = "Unsupported memory region, range, or bank; consult memory_map";
		return false;
	}
	std::vector<uint8_t> bytes(static_cast<size_t>(length));
	if (core->platform(core) == mPLATFORM_GBA) {
		// GBA rawRead8 can enter GPIO/EEPROM handlers in cartridge space.
		// Read the backing store directly to guarantee no peripheral effects.
		size_t size = 0;
		const auto* backing = static_cast<const uint8_t*>(core->getMemoryBlock(core, region->id, &size));
		const uint64_t offset = address - region->start;
		if (!backing || offset + length > size) {
			*error = "Read exceeds memory backing store";
			return false;
		}
		std::copy(backing + offset, backing + offset + length, bytes.begin());
	} else {
		for (int i = 0; i < length; ++i)
			bytes[size_t(i)] = uint8_t(core->rawRead8(core, address + uint32_t(i), segment));
	}
	static const char digits[] = "0123456789abcdef";
	std::string hex;
	hex.reserve(bytes.size() * 2);
	for (uint8_t byte : bytes) {
		hex += digits[byte >> 4];
		hex += digits[byte & 0xF];
	}
	*result = { { "address", address }, { "length", length }, { "segment", segment }, { "hex", hex } };
	return true;
}

bool Bridge::isCheckpointName(const std::string& name) {
	return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](char c) {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
	});
}

bool Bridge::checkpoint(mCore* core, const std::string& method, const Json& args, std::string* error) {
	auto fail = [&](const char* message) {
		*error = message;
		return false;
	};
	const std::string name = toString(field(args, "name"));
	if (!isCheckpointName(name))
		return fail("Checkpoint names must contain 1\xE2\x80\x93"
		            "64 letters, digits, underscores, or hyphens");
	uint32_t crc = 0;
	core->checksum(core, &crc, mCHECKSUM_CRC32);
	char crcText[9];
	std::snprintf(crcText, sizeof(crcText), "%x", unsigned(crc));
	const std::string directory = dataDirectory() + "/checkpoints/" + crcText;
	if (!makeDirectories(directory))
		return fail("Cannot create checkpoint directory");
	const std::string path = directory + "/" + name + ".state";
	bool ok = false;
	if (method == "save_checkpoint") {
		VFile* state = VFileMemChunk(nullptr, 0);
		if (state && mCoreSaveStateNamed(core, state, SAVESTATE_ALL) && state->seek(state, 0, SEEK_SET) == 0) {
			std::vector<uint8_t> bytes(size_t(state->size(state)));
			ok =
			    state->read(state, bytes.data(), bytes.size()) == ssize_t(bytes.size()) && writeAtomically(path, bytes);
		}
		if (state)
			state->close(state);
	} else {
		VFile* file = VFileOpen(path.c_str(), O_RDONLY);
		if (!file)
			return fail("Cannot open checkpoint");
		ok = mCoreLoadStateNamed(core, file, SAVESTATE_ALL);
		file->close(file);
	}
	if (!ok)
		return fail("Checkpoint operation failed (wrong ROM or damaged state)");
	return true;
}

Json Bridge::MovementTracker::progress(uintptr_t client, const Json& start, const Json& end, unsigned directionKeys) {
	if (!directionKeys || !toBool(field(start, "available")))
		return { };
	const Json& startPlayer = field(start, "player");
	const Json& endPlayer = field(end, "player");
	const bool sameMap = field(end, "map") == field(start, "map");
	const bool unchanged = toBool(field(end, "available")) && sameMap &&
	    field(startPlayer, "x") == field(endPlayer, "x") && field(startPlayer, "y") == field(endPlayer, "y");
	const std::string signature = std::to_string(toInt(field(field(start, "map"), "id"))) + ":" +
	    std::to_string(toInt(field(startPlayer, "x"))) + ":" + std::to_string(toInt(field(startPlayer, "y"))) + ":" +
	    std::to_string(directionKeys);
	Stall& stall = m_stalls[client];
	const int repeats = unchanged ? (stall.signature == signature ? stall.count + 1 : 1) : 0;
	stall = { signature, repeats };
	return { { "position_unchanged", unchanged },
	         { "repeated_movement_loop", repeats >= 3 },
	         { "unchanged_attempts", repeats },
	         { "map_changed", !sameMap } };
}
