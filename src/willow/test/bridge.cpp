/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "willow/Bridge.h"

#include <mgba-util/vfs.h>
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#ifdef M_CORE_GB
#include <mgba/gb/core.h>
#endif
#include <mgba/gba/core.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(condition)                                                           \
	do {                                                                           \
		if (!(condition)) {                                                        \
			std::cerr << "Failed line " << __LINE__ << ": " << #condition << '\n'; \
			std::abort();                                                          \
		}                                                                          \
	} while (0)

using namespace Willow;
using Json = Willow::Json;

namespace {

std::string actionError(const std::string& method, const Json& args, bool gba = true, bool paused = false) {
	Bridge::ActionPlan plan;
	std::string error;
	CHECK(!Bridge::parseAction(method, args, gba, paused, &plan, &error));
	return error;
}

Bridge::ActionPlan action(const std::string& method, const Json& args, bool gba = true) {
	Bridge::ActionPlan plan;
	std::string error;
	CHECK(Bridge::parseAction(method, args, gba, false, &plan, &error));
	return plan;
}

std::string stateError(const std::string& method, const Json& args) {
	std::string error;
	CHECK(!Bridge::checkStateAction(method, args, &error));
	return error;
}

void setEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
	_putenv_s(name, value.c_str());
#else
	setenv(name, value.c_str(), 1);
#endif
}

// A blank cartridge: enough for the memory map, reads and save states.
mCore* blankCore(mCore* core, size_t romSize) {
	CHECK(core && core->init(core));
	mCoreInitConfig(core, nullptr);
	VFile* rom = VFileMemChunk(nullptr, romSize);
	CHECK(rom && core->loadROM(core, rom));
	core->reset(core);
	return core;
}

void destroy(mCore* core) {
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

void testProtocol() {
	CHECK(Bridge::pipeName("abc") == "mgba-willow-ai-abc");
	const Json descriptor = Bridge::sessionDescriptor("abc", 42);
	CHECK(descriptor.dump() == R"({"pid":42,"pipe":"mgba-willow-ai-abc","protocol":1,"session_id":"abc"})");

	CHECK(Bridge::isRequestId(1));
	CHECK(Bridge::isRequestId(9007199254740991.0));
	CHECK(Bridge::isRequestId(Json::parse("3.0")));
	CHECK(!Bridge::isRequestId(0));
	CHECK(!Bridge::isRequestId(1.5));
	CHECK(!Bridge::isRequestId("1"));
	CHECK(!Bridge::isRequestId(Json()));
	CHECK(!Bridge::isRequestId(9007199254740992.0));
	CHECK(Bridge::isInteger(-1, -1, 65535));
	CHECK(!Bridge::isInteger(true, 0, 1));
}

void testSession() {
	Bridge::Session empty;
	empty.id = "s";
	empty.objective = "Beat Roxanne";
	empty.aiSpeed = -1;
	empty.title = "ignored without a game";
	empty.humanKeys = 1;
	const Json none = Bridge::sessionJson(empty);
	CHECK(none["platform"] == "none");
	CHECK(none["title"] == "");
	CHECK(none["ai_speed"] == "maximum");
	CHECK(none["human_keys"] == 0);
	CHECK(none["connected"] == false);
	CHECK(none["objective"] == "Beat Roxanne");
	CHECK(none.size() == 17);

	Bridge::Session game = empty;
	game.hasGame = true;
	game.title = "POKEMON EMER";
	game.gba = true;
	game.frame = 123456789012ULL;
	game.openGL = true;
	game.clients = 2;
	game.aiSpeed = 4;
	game.humanKeys = 1U << 5; // Left
	game.aiKeys = (1U << 4) | 1; // Right + A
	const Json running = Bridge::sessionJson(game);
	CHECK(running["platform"] == "GBA");
	CHECK(running["title"] == "POKEMON EMER");
	CHECK(running["frame"] == 123456789012ULL);
	CHECK(running["renderer"] == "opengl");
	CHECK(running["connected"] == true && running["connected_clients"] == 2);
	CHECK(running["ai_speed"] == 4);
	CHECK(running["effective_keys"] == ((1U << 5) | 1));
	CHECK(Bridge::mobileSessionJson(game)["title"] == "Pok\xC3\xA9mon Emerald");
	game.gba = false;
	CHECK(Bridge::mobileSessionJson(game)["title"] == "POKEMON EMER");
	CHECK(Bridge::mobileSessionJson(game)["platform"] == "GB/GBC");
}

void testSpeeds() {
	int speed = 0;
	CHECK(Bridge::parseSpeed("maximum", &speed) && speed == -1);
	CHECK(Bridge::parseSpeed(8, &speed) && speed == 8);
	CHECK(Bridge::parseSpeed(2.0, &speed) && speed == 2);
	CHECK(!Bridge::parseSpeed(3, &speed));
	CHECK(!Bridge::parseSpeed(-1, &speed));
	CHECK(!Bridge::parseSpeed(16, &speed));
	CHECK(!Bridge::parseSpeed("max", &speed));
	CHECK(!Bridge::parseSpeed(Json(), &speed));
	CHECK(Bridge::isMobileSpeed(0) && Bridge::isMobileSpeed(-1) && !Bridge::isMobileSpeed(3));
	CHECK(!Bridge::isAISpeed(0));
	CHECK(Bridge::keyMask(true) == 0x3FF && Bridge::keyMask(false) == 0xFF);
}

void testActions() {
	auto plan = action("act", { { "buttons", { "A", "Up" } }, { "frames", 4 } });
	CHECK(plan.steps.size() == 1 && plan.steps[0].keys == (1U | (1U << 6)) && plan.steps[0].frames == 4);
	CHECK(plan.frames == std::vector<int>({ 4 }));
	CHECK(!plan.sequence && plan.screenshot && plan.directionKeys == (1U << 6));

	plan = action("act_sequence",
	              { { "actions",
	                  Json::array({ { { "buttons", { "Right" } }, { "frames", 8 } },
	                                { { "buttons", Json::array() }, { "frames", 2 } } }) },
	                { "screenshot", false } });
	CHECK(plan.sequence && !plan.screenshot && plan.steps.size() == 2);
	CHECK(plan.directionKeys == (1U << 4) && plan.frames == std::vector<int>({ 8, 2 }));

	plan = action("press", { { "button", "Start" } });
	CHECK(plan.sequence && !plan.screenshot);
	CHECK(plan.frames == std::vector<int>({ 2, 2, 2 }));
	CHECK(plan.steps[0].keys == 0 && plan.steps[1].keys == (1U << 3) && plan.steps[2].keys == 0);
	plan =
	    action("press", { { "button", "L" }, { "hold_frames", 5 }, { "release_frames", 7 }, { "screenshot", true } });
	CHECK(plan.screenshot && plan.frames == std::vector<int>({ 2, 5, 7 }) && plan.steps[1].keys == (1U << 9));

	CHECK(actionError("press", { { "button", 1 } }) ==
	      "Expected button, hold_frames and release_frames (1\xE2\x80\x93"
	      "60)");
	CHECK(actionError("press", { { "button", "A" }, { "hold_frames", 61 } }).rfind("Expected button", 0) == 0);
	// Bad press arguments are reported before a paused game.
	CHECK(
	    actionError("press", { { "button", "A" }, { "release_frames", 0 } }, true, true).rfind("Expected button", 0) ==
	    0);
	CHECK(actionError("press", { { "button", "A" } }, true, true) == "Game paused; resume manually before acting");
	CHECK(actionError("press", { { "button", "Jump" } }) == "Unknown button");
	CHECK(actionError("press", { { "button", "A" }, { "screenshot", 1 } }) == "Expected boolean screenshot");
	CHECK(actionError("act", { { "buttons", { "A" } }, { "frames", 4 } }, true, true) ==
	      "Game paused; resume manually before acting");
	CHECK(actionError("act", { { "buttons", { "A" } }, { "frames", 4 }, { "screenshot", "yes" } }) ==
	      "Expected boolean screenshot");
	CHECK(actionError("act", { { "buttons", { "A" } }, { "frames", 0 } }) ==
	      "Expected buttons array and 1\xE2\x80\x93"
	      "600 frames");
	CHECK(actionError("act", { { "buttons", "A" }, { "frames", 1 } }).rfind("Expected buttons array", 0) == 0);
	CHECK(actionError("act",
	                  { { "buttons", Json::array({ "A", "A", "A", "A", "A", "A", "A", "A", "A", "A", "A" }) },
	                    { "frames", 1 } })
	          .rfind("Expected buttons array", 0) == 0);
	CHECK(actionError("act", { { "buttons", { 1 } }, { "frames", 1 } }) == "Unknown button");
	CHECK(actionError("act", { { "buttons", { "R" } }, { "frames", 1 } }, false) == "L/R are only available on GBA");
	CHECK(action("act", { { "buttons", { "Down" } }, { "frames", 1 } }, false).steps[0].keys == (1U << 7));
	CHECK(actionError("act_sequence", Json::object()) ==
	      "Expected 1\xE2\x80\x93"
	      "200 actions");
	CHECK(actionError("act_sequence", { { "actions", Json::array() } }) ==
	      "Expected 1\xE2\x80\x93"
	      "200 actions");
	Json many = Json::array();
	for (int i = 0; i < 201; ++i)
		many.push_back({ { "buttons", Json::array() }, { "frames", 1 } });
	CHECK(actionError("act_sequence", { { "actions", many } }) ==
	      "Expected 1\xE2\x80\x93"
	      "200 actions");
	CHECK(actionError("act_sequence", { { "actions", Json::array({ 5 }) } }).rfind("Expected buttons array", 0) == 0);
	CHECK(actionError("act_sequence",
	                  { { "actions",
	                      Json::array({ { { "buttons", Json::array() }, { "frames", 400 } },
	                                    { { "buttons", Json::array() }, { "frames", 201 } } }) } }) ==
	      "Sequence exceeds 600 total frames");
	CHECK(actionError("observe", Json::object()) == "Unknown method");

	CHECK(Bridge::completedSteps(100, { 2, 5, 7 }, 2).dump() ==
	      R"([{"action_end_frame":102,"action_start_frame":100},{"action_end_frame":107,"action_start_frame":102}])");
	CHECK(Bridge::completedSteps(100, { 2 }, 9).size() == 1);
	CHECK(Bridge::completedSteps(100, { }, 0).dump() == "[]");
}

void testArguments() {
	std::string error;
	CHECK(Bridge::checkStateAction("move_to", { { "x", 3 }, { "y", 511 }, { "avoid_trainers", true } }, &error));
	CHECK(Bridge::checkStateAction("wait_until", { { "condition", "dialogue" }, { "stable_frames", 60 } }, &error));
	CHECK(stateError("move_to", { { "x", 3 }, { "y", 4 }, { "max_frames", 601 } }) ==
	      "Expected 1\xE2\x80\x93"
	      "600 max_frames and boolean screenshot");
	CHECK(stateError("wait_until", { { "condition", "dialogue" }, { "screenshot", 0 } }).rfind("Expected 1", 0) == 0);
	CHECK(stateError("move_to", { { "x", 512 }, { "y", 4 } }) ==
	      "Expected map coordinates x/y 0\xE2\x80\x93"
	      "511");
	CHECK(stateError("move_to", { { "x", 1 } }) ==
	      "Expected map coordinates x/y 0\xE2\x80\x93"
	      "511");
	CHECK(stateError("move_to", { { "x", 1 }, { "y", 1 }, { "avoid_trainers", "no" } }) ==
	      "Expected boolean avoid_trainers");
	CHECK(stateError("wait_until", { { "condition", "battle" } }) ==
	      "Unsupported condition or stable_frames (1\xE2\x80\x93"
	      "60)");
	CHECK(stateError("wait_until", { { "condition", 1 } }).rfind("Unsupported condition", 0) == 0);
	CHECK(stateError("wait_until", { { "condition", "overworld_ready" }, { "stable_frames", 0 } })
	          .rfind("Unsupported", 0) == 0);

	CHECK(Bridge::checkObserve("observe", { { "screenshot", false } }, &error));
	CHECK(Bridge::checkObserve("get_local_map", { { "radius", 16 } }, &error));
	CHECK(!Bridge::checkObserve("observe", { { "screenshot", "no" } }, &error) &&
	      error == "Expected boolean screenshot");
	CHECK(!Bridge::checkObserve("get_local_map", { { "radius", 17 } }, &error) &&
	      error ==
	          "Expected radius 1\xE2\x80\x93"
	          "16");
	CHECK(Bridge::checkObserve("observe", { { "radius", 17 } }, &error));

	CHECK(Bridge::isObjective(std::string(512, 'a')));
	CHECK(!Bridge::isObjective(std::string(513, 'a')));
	std::string accents;
	for (int i = 0; i < 512; ++i)
		accents += "\xC3\xA9"; // 512 code units in 1024 bytes
	CHECK(Bridge::isObjective(accents));
	std::string emoji;
	for (int i = 0; i < 257; ++i)
		emoji += "\xF0\x9F\x8E\xAE"; // 514 code units
	CHECK(!Bridge::isObjective(emoji));
	CHECK(!Bridge::isObjective(5));

	CHECK(Bridge::isCheckpointName("m16-route110_east"));
	CHECK(Bridge::isCheckpointName(std::string(64, 'x')));
	CHECK(!Bridge::isCheckpointName(std::string(65, 'x')));
	CHECK(!Bridge::isCheckpointName(""));
	CHECK(!Bridge::isCheckpointName("../escape"));
	CHECK(!Bridge::isCheckpointName("name\n"));
	CHECK(!Bridge::isCheckpointName("a b"));
}

Json overworld(int map, int x, int y) {
	return { { "available", true },
	         { "map", { { "id", map }, { "name", "Route" } } },
	         { "player", { { "x", x }, { "y", y } } } };
}

void testMovement() {
	Bridge::MovementTracker tracker;
	const unsigned Left = 1U << 5;
	CHECK(tracker.progress(1, overworld(1, 5, 5), overworld(1, 5, 5), 0).is_null());
	CHECK(tracker.progress(1, { { "available", false } }, overworld(1, 5, 5), Left).is_null());

	Json first = tracker.progress(1, overworld(1, 5, 5), overworld(1, 5, 5), Left);
	CHECK(first.dump() ==
	      R"({"map_changed":false,"position_unchanged":true,"repeated_movement_loop":false,"unchanged_attempts":1})");
	CHECK(tracker.progress(1, overworld(1, 5, 5), overworld(1, 5, 5), Left)["unchanged_attempts"] == 2);
	// Each client keeps its own count.
	CHECK(tracker.progress(2, overworld(1, 5, 5), overworld(1, 5, 5), Left)["unchanged_attempts"] == 1);
	Json third = tracker.progress(1, overworld(1, 5, 5), overworld(1, 5, 5), Left);
	CHECK(third["unchanged_attempts"] == 3 && third["repeated_movement_loop"] == true);
	// Another direction from the same tile starts again.
	CHECK(tracker.progress(1, overworld(1, 5, 5), overworld(1, 5, 5), 1U << 4)["unchanged_attempts"] == 1);
	// Moving resets the count.
	Json moved = tracker.progress(1, overworld(1, 5, 5), overworld(1, 4, 5), Left);
	CHECK(moved["position_unchanged"] == false && moved["unchanged_attempts"] == 0);
	CHECK(tracker.progress(1, overworld(1, 5, 5), overworld(1, 5, 5), Left)["unchanged_attempts"] == 1);
	Json warp = tracker.progress(1, overworld(1, 5, 5), overworld(2, 5, 5), Left);
	CHECK(warp["map_changed"] == true && warp["position_unchanged"] == false);
	// The end state may be unreadable mid-transition.
	CHECK(tracker.progress(1, overworld(1, 5, 5), { { "available", false } }, Left)["map_changed"] == true);

	tracker.progress(3, overworld(1, 5, 5), overworld(1, 5, 5), Left);
	tracker.forget(3);
	CHECK(tracker.progress(3, overworld(1, 5, 5), overworld(1, 5, 5), Left)["unchanged_attempts"] == 1);
	tracker.clear();
	CHECK(tracker.progress(3, overworld(1, 5, 5), overworld(1, 5, 5), Left)["unchanged_attempts"] == 1);
}

const Json* region(const Json& map, const char* name) {
	for (const auto& entry : map["regions"])
		if (entry["name"] == name)
			return &entry;
	return nullptr;
}

void testGBAMemory(const std::string& data) {
	mCore* core = blankCore(GBACoreCreate(), 0x1000);
	const Json map = Bridge::memoryMap(core);
	const Json* wram = region(map, "wram");
	CHECK(wram && (*wram)["readable"] == true && (*wram)["start"] == 0x02000000);
	CHECK(region(map, "io") && (*region(map, "io"))["readable"] == false);
	CHECK(region(map, "cart0") && (*region(map, "cart0"))["readable"] == true);

	core->rawWrite8(core, 0x02000010, -1, 0xAB);
	core->rawWrite8(core, 0x02000011, -1, 0x01);
	Json result;
	std::string error;
	CHECK(Bridge::readMemory(core, { { "address", 0x02000010 }, { "length", 3 } }, &result, &error));
	CHECK(result.dump() == R"({"address":33554448,"hex":"ab0100","length":3,"segment":-1})");
	CHECK(!Bridge::readMemory(core, { { "address", 0x04000000 }, { "length", 4 } }, &result, &error));
	CHECK(error == "Unsupported memory region, range, or bank; consult memory_map");
	// A read must not run past the end of the region.
	CHECK(!Bridge::readMemory(core, { { "address", 0x0203FFFF }, { "length", 2 } }, &result, &error));
	CHECK(!Bridge::readMemory(core, { { "address", 0x02000000 }, { "length", 4097 } }, &result, &error));
	CHECK(error == "Invalid address, length, or segment");
	CHECK(!Bridge::readMemory(core, { { "address", -1 }, { "length", 1 } }, &result, &error));
	CHECK(
	    !Bridge::readMemory(core, { { "address", 0x02000000 }, { "length", 1 }, { "segment", -2 } }, &result, &error));
	CHECK(!Bridge::readMemory(core, { { "length", 1 } }, &result, &error));

	// Checkpoints go under <LOCALAPPDATA>/mgba-willow/ai/checkpoints/<crc32>.
	CHECK(Bridge::dataDirectory() == data + "/mgba-willow/ai");
	CHECK(Bridge::checkpoint(core, "save_checkpoint", { { "name", "test-1" } }, &error));
	uint32_t crc = 0;
	core->checksum(core, &crc, mCHECKSUM_CRC32);
	char crcText[9];
	std::snprintf(crcText, sizeof(crcText), "%x", unsigned(crc));
	const std::filesystem::path saved(data + "/mgba-willow/ai/checkpoints/" + crcText + "/test-1.state");
	CHECK(std::filesystem::is_regular_file(saved) && std::filesystem::file_size(saved) > 0);
	CHECK(!std::filesystem::exists(saved.string() + ".tmp"));
	core->rawWrite8(core, 0x02000010, -1, 0x00);
	CHECK(Bridge::checkpoint(core, "load_checkpoint", { { "name", "test-1" } }, &error));
	CHECK(Bridge::readMemory(core, { { "address", 0x02000010 }, { "length", 1 } }, &result, &error));
	CHECK(result["hex"] == "ab");
	// Saving again replaces the file.
	CHECK(Bridge::checkpoint(core, "save_checkpoint", { { "name", "test-1" } }, &error));
	CHECK(!Bridge::checkpoint(core, "load_checkpoint", { { "name", "missing" } }, &error));
	CHECK(error == "Cannot open checkpoint");
	CHECK(!Bridge::checkpoint(core, "save_checkpoint", { { "name", "../up" } }, &error));
	CHECK(error ==
	      "Checkpoint names must contain 1\xE2\x80\x93"
	      "64 letters, digits, underscores, or hyphens");
	CHECK(!Bridge::checkpoint(core, "load_checkpoint", Json::object(), &error));
	// A damaged state is refused.
	{
		VFile* bad = VFileOpen(saved.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC);
		CHECK(bad);
		bad->write(bad, "junk", 4);
		bad->close(bad);
	}
	CHECK(!Bridge::checkpoint(core, "load_checkpoint", { { "name", "test-1" } }, &error));
	CHECK(error == "Checkpoint operation failed (wrong ROM or damaged state)");
	destroy(core);
}

#ifdef M_CORE_GB
void testGBMemory() {
	mCore* core = blankCore(GBCoreCreate(), 0x8000);
	const Json map = Bridge::memoryMap(core);
	const Json* hram = region(map, "hram");
	CHECK(hram && (*hram)["readable"] == true);
	core->rawWrite8(core, 0xFF90, -1, 0x5A);
	Json result;
	std::string error;
	CHECK(Bridge::readMemory(core, { { "address", 0xFF90 }, { "length", 1 } }, &result, &error));
	CHECK(result["hex"] == "5a" && result["segment"] == -1);
	destroy(core);
}
#endif

} // namespace

int main() {
	testProtocol();
	testSession();
	testSpeeds();
	testActions();
	testArguments();
	testMovement();

	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root = std::filesystem::temp_directory_path() / ("willow-bridge-test-" + std::to_string(stamp));
	std::filesystem::create_directories(root);
	const std::string data = root.generic_string();
	setEnvironment("LOCALAPPDATA", data);
	testGBAMemory(data);
#ifdef M_CORE_GB
	testGBMemory();
#endif
	std::error_code ignored;
	std::filesystem::remove_all(root, ignored);
	std::cout << "bridge: ok\n";
	return 0;
}
