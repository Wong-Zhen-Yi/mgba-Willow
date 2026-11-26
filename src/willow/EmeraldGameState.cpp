#include "EmeraldGameState.h"

#include <mgba/core/core.h>
#include <mgba-util/sha1.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>

using namespace Willow;

namespace {
constexpr uint32_t Save1 = 0x03005D8C, Save2 = 0x03005D90;
constexpr uint32_t MapHeader = 0x02037318, Objects = 0x02037350, Avatar = 0x02037590;
constexpr uint32_t Main = 0x030022C0, Grid = 0x03005DC0;
Json coords(Point p) { return {{"x", p.x}, {"y", p.y}}; }
void put(Json& object, const char* key, Json value) { object[key] = std::move(value); }
std::string sha1Hex(const Bytes& bytes) {
	uint8_t digest[20];
	sha1Buffer(bytes.data(), bytes.size(), digest);
	static const char hex[] = "0123456789abcdef";
	std::string result;
	for (uint8_t byte : digest) {
		result += hex[byte >> 4];
		result += hex[byte & 15];
	}
	return result;
}
// The replacement character, for text the decoder cannot represent.
const char* const Unknown = "\xEF\xBF\xBD";
std::string trimmed(const std::string& text) {
	const char* space = " \t\n\v\f\r";
	size_t start = text.find_first_not_of(space);
	if (start == std::string::npos) return {};
	return text.substr(start, text.find_last_not_of(space) - start + 1);
}
unsigned direction(Point from, Point to) {
	if (to.x > from.x) return 16;
	if (to.x < from.x) return 32;
	if (to.y < from.y) return 64;
	return 128;
}
Bytes backing(mCore* core, const char* name) {
	const mCoreMemoryBlock* blocks = nullptr;
	size_t count = core->listMemoryBlocks(core, &blocks);
	for (size_t i = 0; i < count; ++i) {
		if (!blocks[i].internalName || std::strcmp(blocks[i].internalName, name) != 0) continue;
		size_t size = 0;
		const auto* data = static_cast<const uint8_t*>(core->getMemoryBlock(core, blocks[i].id, &size));
		if (data && size <= 0x2000000) return Bytes(data, data + size);
	}
	return {};
}
bool special(int b) {
	// Conservative initial on-foot planner: water, directional barriers, ledges,
	// forced movement, holes, ice puzzles and bridges require explicit traversal.
	return b < 0 || (b >= 0x10 && b <= 0x15) || b == 0x0D || b == 0x0E || b == 0x0F ||
		b == 0x20 || b == 0x26 || b == 0x27 || b == 0x29 || b == 0x22 || b == 0x2A ||
		(b >= 0x30 && b <= 0x5F) || (b >= 0x70 && b <= 0x7F) || b >= 0xA0;
}
}

const char* EmeraldGameState::romSha1() { return "f3ae088181bf583e55daf962a92bb46f4f1d07b7"; }

bool EmeraldGameState::supportsSha1(const std::string& sha1) {
	// This alternate English dump was checked against a live RAM/ROM snapshot:
	// save blocks, map/grid, callbacks, objects and encrypted party data retain
	// the retail layout. Do not accept arbitrary BPEE headers or ROM hacks.
	return sha1 == romSha1() || sha1 == "4c743011d7f9af0fbc1ef1de7bff157dde718f56";
}

void EmeraldGameState::identify(mCore* core) {
	*this = EmeraldGameState();
	if (core->platform(core) != mPLATFORM_GBA) return;
	m_rom = backing(core, "cart0");
	// Hash the loaded backing store, including patches, rather than the original file.
	m_sha1 = sha1Hex(m_rom);
	// mGBA maps live cartridge GPIO into these three header halfwords (see
	// gba/cart/gpio.c). They are zero in both supported dumps. Normalize only
	// the copied buffer; never alter the emulator's ROM or clock registers.
	if (m_rom.size() >= 0xCA) std::fill(m_rom.begin() + 0xC4, m_rom.begin() + 0xCA, '\0');
	m_normalizedSha1 = sha1Hex(m_rom);
	m_supported = supportsSha1(m_normalizedSha1);
	if (!m_supported) m_rom.clear();
}

void EmeraldGameState::capture(mCore* core, uint64_t frame, unsigned human, unsigned ai, unsigned effective) {
	if (!m_supported) return;
	if (frame < m_frame) { m_events = Json::array(); m_lastMessage.clear(); m_saveConfirmed = false; }
	m_wram = backing(core, "wram");
	m_iwram = backing(core, "iwram");
	m_frame = frame;
	m_human = human;
	m_ai = ai;
	m_effective = effective;
	recordEvents();
}

void EmeraldGameState::setSnapshot(const Bytes& rom, const Bytes& wram, const Bytes& iwram, uint64_t frame) {
	if (frame < m_frame) { m_events = Json::array(); m_lastMessage.clear(); m_saveConfirmed = false; }
	m_rom = rom; m_wram = wram; m_iwram = iwram; m_frame = frame;
	recordEvents();
}

bool EmeraldGameState::contains(uint32_t a, uint32_t length) const {
	return (a >= 0x02000000 && uint64_t(a) + length <= 0x02000000ULL + m_wram.size()) ||
		(a >= 0x03000000 && uint64_t(a) + length <= 0x03000000ULL + m_iwram.size()) ||
		(a >= 0x08000000 && uint64_t(a) + length <= 0x08000000ULL + m_rom.size());
}

uint32_t EmeraldGameState::read(uint32_t a, int length) const {
	if (!contains(a, length)) return 0;
	const Bytes* bytes = &m_rom;
	uint32_t base = 0x08000000;
	if (a < 0x03000000) { bytes = &m_wram; base = 0x02000000; }
	else if (a < 0x08000000) { bytes = &m_iwram; base = 0x03000000; }
	uint32_t value = 0;
	for (int i = 0; i < length; ++i) value |= uint32_t((*bytes)[a - base + i]) << (i * 8);
	return value;
}

uint32_t EmeraldGameState::playerObject() const {
	int index = read(Avatar + 5);
	if (index >= 16) return 0;
	uint32_t a = Objects + index * 36;
	return (read(a) & 1) && (read(a + 2) & 1) ? a : 0;
}

uint32_t EmeraldGameState::layout() const {
	uint32_t p = read(MapHeader, 4);
	return contains(p, 24) && read(p, 4) > 0 && read(p, 4) <= 512 && read(p + 4, 4) > 0 && read(p + 4, 4) <= 512 ? p : 0;
}

bool EmeraldGameState::ready() const {
	// SaveBlock1 and SaveBlock2 must live wholly within EWRAM.
	uint32_t s1 = read(Save1, 4), s2 = read(Save2, 4);
	return s1 >= 0x02000000 && uint64_t(s1) + 0x3D88 <= 0x02040000 &&
		s2 >= 0x02000000 && uint64_t(s2) + 0xF2C <= 0x02040000 && contains(s1, 0x3D88) && contains(s2, 0xF2C) && layout() && playerObject();
}

Point EmeraldGameState::position() const {
	uint32_t p = playerObject();
	return p ? Point(int16_t(read(p + 16, 2)) - 7, int16_t(read(p + 18, 2)) - 7) : Point(-1, -1);
}
int EmeraldGameState::mapId() const { uint32_t s = read(Save1, 4); return ready() ? int((read(s + 4) << 8) | read(s + 5)) : -1; }
bool EmeraldGameState::moving() const {
	return read(Avatar + 3) != 0 || (read(playerObject()) & 2) ||
	    ((read(playerObject()) & 0x40) && !(read(playerObject()) & 0x80));
}

std::string EmeraldGameState::interaction() const {
	uint32_t callback = read(Main + 4, 4) & ~1U;
	if ((read(Main + 0x439) & 2) || callback == 0x08038420 || callback == 0x08036760 || callback == 0x080367D4) return "battle";
	if (!ready()) return "unavailable";
	if (callback != 0x08085E5C) return "transition_or_menu";
	if (read(0x020375BC)) return "dialogue";
	for (int i = 0; i < 16; ++i) {
		uint32_t task = 0x03005E00 + i * 40;
		uint32_t function = read(task, 4) & ~1U;
		if (read(task + 4) && (function == 0x0809FA34 || function == 0x0809FFD0)) return "menu";
	}
	if (read(0x03000F2C) || read(Avatar + 6) || (read(playerObject() + 1) & 1)) return "scripted";
	return "overworld";
}

bool EmeraldGameState::battleMenuReady() const {
	return menu().value("kind", Json()) == "battle_action";
}

std::string EmeraldGameState::text(uint32_t address, int limit) const {
	std::string result;
	for (int i = 0; i < limit; ++i) {
		if (!contains(address + i)) return {};
		int c = read(address + i);
		if (c == 0xFF) return trimmed(result);
		if (c == 0xFD) return {}; // Unexpanded placeholders are not evidence.
		if (c == 0xFC) {
			if (++i >= limit || !contains(address + i)) return {};
			int code = read(address + i);
			if (code < 1 || code > 0x18) return {};
			int args = code == 4 ? 3 : (code == 0x0B || code == 0x10) ? 2 :
				(code <= 6 || code == 8 || code == 0x0C || code == 0x0D || code == 0x0E || (code >= 0x11 && code <= 0x14)) ? 1 : 0;
			if (i + args >= limit || !contains(address + i, args + 1)) return {};
			if (code == 0x0C) result += Unknown;
			i += args; continue;
		}
		if (c >= 0xBB && c <= 0xD4) result += char('A' + c - 0xBB);
		else if (c >= 0xD5 && c <= 0xEE) result += char('a' + c - 0xD5);
		else if (c >= 0xA1 && c <= 0xAA) result += char('0' + c - 0xA1);
		else if (c == 0) result += ' ';
		else if (c >= 0xFA) result += '\n';
		else {
			switch (c) {
			case 0xAB: result += '!'; break;
			case 0xAC: result += '?'; break;
			case 0xAD: result += '.'; break;
			case 0xAE: result += '-'; break;
			case 0xB4: result += '\''; break;
			case 0xB8: result += ','; break;
			case 0xF0: result += ':'; break;
			default: result += Unknown; break;
			}
		}
	}
	return {}; // Unterminated memory must not become a message.
}

Json EmeraldGameState::menu() const {
	Json result{{"kind", "none"}, {"input_ready", false}};
	if (!m_supported || !hasSnapshot() || (read(0x02037FDB) & 0x80)) return result;
	uint32_t callback = read(Main + 4, 4) & ~1U;
	if (callback == 0x081AAD5C) {
		int pocket = read(0x0203CE5D);
		put(result, "kind", "bag");
		if (pocket < 5) {
			put(result, "pocket", pocket);
			put(result, "cursor", int(read(0x0203CE60 + pocket * 2, 2)));
			put(result, "scroll", int(read(0x0203CE6A + pocket * 2, 2)));
		}
		return result; // Context menus and animations still require observation.
	}
	if (callback == 0x081B01B0) {
		put(result, "kind", "party");
		int slot = read(0x0203CED1);
		if (slot < 6) put(result, "selected_party_slot", slot);
		return result;
	}
	if (interaction() == "menu") {
		bool saving = (read(0x03005DF4, 4) & ~1U) == 0x0809FE44;
		for (int i = 0; i < 16; ++i) {
			uint32_t task = 0x03005E00 + i * 40;
			if (read(task + 4) && (read(task, 4) & ~1U) == 0x0809FFD0) saving = true;
		}
		put(result, "kind", saving ? "save" : "start");
		if (saving) {
			uint32_t save = read(0x0203761C, 4) & ~1U;
			std::string phase = "processing";
			if (save == 0x080A0108) phase = "confirm";
			else if (save == 0x080A01EC) phase = "confirm_overwrite";
			else if (save == 0x080A024C) phase = "writing";
			else if (save == 0x080A02B0) phase = "success_message";
			else if (save == 0x080A02D8) phase = "saved";
			else if (save == 0x080A02FC || save == 0x080A0324) phase = "error";
			put(result, "phase", phase);
		} else put(result, "cursor", int(read(0x0203760E)));
		return result;
	}
	if (interaction() != "battle" || (callback != 0x08038420 && callback != 0x08036760 && callback != 0x080367D4)) return result;
	int count = read(0x0202406C);
	if (count < 1 || count > 4) return result;
	for (int i = 0; i < count; ++i) {
		int position = read(0x02024076 + i);
		if (position > 3 || (position & 1) || !(read(0x02024068, 4) & (1U << i)) || (read(0x02024210) & (1U << i))) continue;
		uint32_t function = read(0x03005D60 + i * 4, 4) & ~1U;
		std::string kind;
		int cursor = -1;
		if (function == 0x08057588) { kind = "battle_action"; cursor = read(0x020244AC + i); }
		else if (function == 0x08057BFC) { kind = "battle_move"; cursor = read(0x020244B0 + i); }
		else if (function == 0x08057824) kind = "battle_target";
		else continue;
		if (cursor >= 4) continue;
		result = {{"kind", kind}, {"input_ready", true}, {"battler_id", i}, {"position", position}};
		if (cursor >= 0) put(result, "cursor", cursor);
		if (kind == "battle_move") {
			uint32_t info = 0x02023064 + i * 512 + 4;
			int move = read(info + cursor * 2, 2);
			put(result, "selected_move", Json{{"id", move}, {"name", move <= 354 ? text(0x0831977C + move * 13, 13) : std::string()},
				{"pp", int(read(info + 8 + cursor))}, {"max_pp", int(read(info + 12 + cursor))}});
		}
		if (kind == "battle_target") {
			int target = read(0x03005D74);
			bool valid = target < count && read(0x02024076 + target) <= 3 && !(read(0x02024210) & (1U << target));
			put(result, "target_available", valid);
			if (valid) {
				int targetPosition = read(0x02024076 + target);
				put(result, "target", Json{{"battler_id", target}, {"position", targetPosition},
					{"side", (targetPosition & 1) ? "opponent" : "player"}, {"slot", targetPosition / 2}});
			}
		}
		return result;
	}
	return result;
}

Json EmeraldGameState::battle() const {
	Json result{{"available", false}};
	if (interaction() != "battle" || !hasSnapshot()) return result;
	int count = read(0x0202406C);
	if (count < 1 || count > 4) return result;
	Json battlers = Json::array();
	const char* stages[] = {"hp", "attack", "defense", "speed", "special_attack", "special_defense", "accuracy", "evasion"};
	for (int i = 0; i < count; ++i) {
		int pos = read(0x02024076 + i);
		uint32_t mon = 0x02024084 + i * 0x58;
		int hp = read(mon + 0x28, 2), maxHp = read(mon + 0x2C, 2);
		bool valid = pos <= 3 && maxHp > 0 && hp <= maxHp;
		Json battler{{"id", i}, {"available", valid}, {"absent", bool(read(0x02024210) & (1U << i))}};
		if (valid) {
			int species = read(mon, 2);
			Json stats = Json::object();
			for (int j = 1; j < 8; ++j) { int value = read(mon + 0x18 + j); if (value <= 12) put(stats, stages[j], value - 6); }
			put(battler, "position", pos); put(battler, "side", (pos & 1) ? "opponent" : "player"); put(battler, "slot", pos / 2);
			put(battler, "party_slot", int(read(0x0202406E + i * 2, 2)));
			put(battler, "species_id", species); put(battler, "species_name", species < 412 ? text(0x083185C8 + species * 11, 11) : std::string());
			put(battler, "hp", hp); put(battler, "max_hp", maxHp); put(battler, "level", int(read(mon + 0x2A)));
			put(battler, "status", uint64_t(read(mon + 0x4C, 4)));
			put(battler, "volatile_status", uint64_t(read(mon + 0x50, 4)));
			put(battler, "confusion_turns", int(read(mon + 0x50, 4) & 7)); put(battler, "stat_changes", stats);
			Json moves = Json::array();
			for (int j = 0; j < 4; ++j) moves.push_back(Json{{"id", int(read(mon + 0x0C + j * 2, 2))}, {"pp", int(read(mon + 0x24 + j))}});
			put(battler, "moves", moves);
		}
		battlers.push_back(battler);
	}
	put(result, "available", true); put(result, "battlers", battlers);
	put(result, "type_flags", uint64_t(read(0x02022FEC, 4))); put(result, "outcome", int(read(0x0202433A)));
	return result;
}

void EmeraldGameState::recordEvents() {
	if (!m_supported || !hasSnapshot()) return;
	uint32_t buffer = interaction() == "battle" ? 0x02022E2C : 0x02021FC4;
	int size = interaction() == "battle" ? 300 : 1000;
	uint32_t printer = 0x020201B0; // Message window 0; not PP/type windows.
	uint32_t current = read(printer, 4);
	std::string message;
	if (read(printer + 0x1B) && read(printer + 4) == 0 && current >= buffer && current < buffer + size)
		message = text(buffer, size);
	if (!message.empty() && message != m_lastMessage) {
		m_events.push_back(Json{{"type", "message"}, {"frame", uint64_t(m_frame)}, {"text", message}, {"source", "expanded_message_buffer"}, {"map_id", mapId()}});
		m_lastMessage = message;
	}
	if (message.empty()) m_lastMessage.clear();
	bool saved = menu().value("kind", Json()) == "save" && (read(0x0203761C, 4) & ~1U) == 0x080A02D8;
	if (saved && !m_saveConfirmed) m_events.push_back(Json{{"type", "save_confirmed"}, {"frame", uint64_t(m_frame)}, {"map_id", mapId()}, {"position", coords(position())}});
	m_saveConfirmed = saved;
	while (m_events.size() > 16) m_events.erase(m_events.begin());
}

bool EmeraldGameState::condition(const std::string& name) const {
	if (name == "battle_menu_ready") return battleMenuReady();
	if (name == "battle_move_ready") return menu().value("kind", Json()) == "battle_move";
	if (name == "battle_target_ready") return menu().value("kind", Json()) == "battle_target";
	if (name == "dialogue") return interaction() == "dialogue";
	if (name == "overworld_ready" || name == "map_transition_complete")
		return ready() && interaction() == "overworld" && !moving() && !(read(Avatar) & 0x40) &&
		    !(read(0x02037FD4 + 7) & 0x80);
	return false;
}

int EmeraldGameState::tile(Point p) const {
	uint32_t l = layout();
	if (!l || p.x < 0 || p.y < 0 || p.x >= int(read(l, 4)) || p.y >= int(read(l + 4, 4))) return -1;
	uint32_t w = read(Grid, 4), h = read(Grid + 4, 4), base = read(Grid + 8, 4);
	if (w != read(l, 4) + 15 || h != read(l + 4, 4) + 14 || w * h > 0x2800 || !contains(base, w * h * 2)) return -1;
	uint32_t address = base + ((p.y + 7) * w + p.x + 7) * 2;
	return int(read(address, 2));
}

int EmeraldGameState::behavior(int metatile) const {
	uint32_t l = layout();
	if (!l) return -1;
	uint32_t tileset = read(l + (metatile < 512 ? 16 : 20), 4);
	if (!contains(tileset, 24)) return -1;
	uint32_t attr = read(tileset + 16, 4) + (metatile % 512) * 2;
	return contains(attr, 2) ? int(read(attr, 2) & 0xFF) : -1;
}

bool EmeraldGameState::occupied(Point point) const {
	for (int i = 0; i < 16; ++i) {
		uint32_t o = Objects + i * 36;
		if (!(read(o) & 1) || o == playerObject() || int((read(o + 10) << 8) | read(o + 9)) != mapId()) continue;
		for (int offset : {16, 20}) if (point == Point(int16_t(read(o + offset, 2)) - 7, int16_t(read(o + offset + 2, 2)) - 7)) return true;
	}
	return false;
}

Json EmeraldGameState::tileState(Point p) const {
	int t = tile(p), b = t >= 0 ? behavior(t & 0x3FF) : -1;
	auto result = coords(p);
	put(result, "known", t >= 0 && b >= 0);
	if (t < 0) return result;
	put(result, "metatile_id", t & 0x3FF);
	put(result, "collision", (t >> 10) & 3);
	put(result, "elevation", t >> 12);
	put(result, "behavior", b);
	put(result, "occupied", occupied(p));
	put(result, "special_traversal", special(b));
	put(result, "trainer_risk", trainerRisk(p));
	if (b >= 0x38 && b <= 0x3F) {
		const char* ledges[] = {"east", "west", "north", "south", "northeast", "northwest", "southeast", "southwest"};
		put(result, "one_way_ledge", ledges[b - 0x38]);
	}
	if (b == 0x61) put(result, "ladder", true);
	return result;
}

bool EmeraldGameState::canWalk(Point from, Point to, bool destination) const {
	Point delta = to - from;
	int distance = std::abs(delta.x) + std::abs(delta.y);
	if (distance == 2 && (!delta.x || !delta.y)) {
		Point step(delta.x / 2, delta.y / 2);
		int middle = tile(from + step), landing = tile(to), start = tile(from);
		int expected = step.x == 1 ? 0x38 : step.x == -1 ? 0x39 : step.y == -1 ? 0x3A : 0x3B;
		if (start < 0 || middle < 0 || landing < 0 || behavior(middle & 0x3FF) != expected ||
			(landing & 0xC00) || occupied(from + step) || occupied(to) || special(behavior(start & 0x3FF)) || special(behavior(landing & 0x3FF))) return false;
		int e1 = start >> 12, e2 = landing >> 12;
		if (e1 && e2 && e1 != 15 && e2 != 15 && e1 != e2) return false;
		int b = behavior(landing & 0x3FF);
		return b < 0x60 || b > 0x6F; // A jump must land on ordinary ground.
	}
	if (distance != 1) return false;
	int a = tile(from), b = tile(to);
	// Animated doors are collision tiles, activated by walking north into a warp.
	if (destination && delta == Point(0, -1) && a >= 0 && b >= 0 && behavior(b & 0x3FF) == 0x69 && !occupied(to) && !special(behavior(a & 0x3FF))) {
		uint32_t events = read(MapHeader + 4, 4);
		if (contains(events, 20)) {
			int count = read(events + 1); uint32_t warps = read(events + 8, 4);
			if (count <= 64 && contains(warps, count * 8)) for (int i = 0; i < count; ++i)
				if (to == Point(int16_t(read(warps + i * 8, 2)), int16_t(read(warps + i * 8 + 2, 2)))) return true;
		}
		return false;
	}
	if (a < 0 || b < 0 || (b & 0xC00) || occupied(to) || special(behavior(a & 0x3FF)) || special(behavior(b & 0x3FF))) return false;
	int e1 = a >> 12, e2 = b >> 12;
	if (e1 && e2 && e1 != 15 && e2 != 15 && e1 != e2) return false;
	int beh = behavior(b & 0x3FF);
	// Exits can be requested as the destination but are never crossed incidentally.
	if (!destination && beh >= 0x60 && beh <= 0x6F) return false;
	return true;
}

bool EmeraldGameState::trainerRisk(Point point) const {
	// Conservative potential sightlines: rotating and defeated trainers are not
	// distinguished. Do not claim that a battle is guaranteed or walls hide us.
	for (int i = 0; i < 16; ++i) {
		uint32_t o = Objects + i * 36;
		if (!(read(o) & 1) || o == playerObject() || int((read(o + 10) << 8) | read(o + 9)) != mapId() || !read(o + 7))
			continue;
		Point p(int16_t(read(o + 16, 2)) - 7, int16_t(read(o + 18, 2)) - 7);
		Point delta = point - p;
		if ((!delta.x || !delta.y) && delta.manhattanLength() <= int(read(o + 29)))
			return true;
	}
	return false;
}

std::vector<Point> EmeraldGameState::pathTo(Point target, int radius, bool avoidTrainers) const {
	const Point origin = position();
	if (origin == target) return {};
	std::deque<Point> queue{origin};
	std::unordered_map<int, Point> parent;
	auto key = [](Point p) { return p.y * 1024 + p.x; };
	parent.emplace(key(origin), origin);
	while (!queue.empty()) {
		Point p = queue.front();
		queue.pop_front();
		for (Point d : {Point(1, 0), Point(-1, 0), Point(0, 1), Point(0, -1)}) {
			Point next = p + d;
			int t = tile(next);
			if (t >= 0 && behavior(t & 0x3FF) >= 0x38 && behavior(t & 0x3FF) <= 0x3B) next += d;
			if (std::abs(next.x - origin.x) > radius || std::abs(next.y - origin.y) > radius || parent.count(key(next)) || !canWalk(p, next, next == target)) continue;
			if (avoidTrainers && (trainerRisk(next) || ((next - p).manhattanLength() == 2 && trainerRisk(p + d))))
				continue;
			parent.emplace(key(next), p);
			if (next == target) {
				std::vector<Point> path;
				for (Point n = next; n != origin; n = parent.at(key(n))) path.push_back(n);
				std::reverse(path.begin(), path.end());
				return path;
			}
			queue.push_back(next);
		}
	}
	return {};
}

Json EmeraldGameState::localMap(int radius) const {
	if (!ready()) return {{"available", false}, {"reason", "No initialized map snapshot"}};
	Json tiles = Json::array(), npcs = Json::array(), exits = Json::array(), connections = Json::array();
	Point pos = position();
	for (int y = pos.y - radius; y <= pos.y + radius; ++y)
		for (int x = pos.x - radius; x <= pos.x + radius; ++x) tiles.push_back(tileState({x, y}));
	for (int i = 0; i < 16; ++i) {
		uint32_t o = Objects + i * 36;
		if (!(read(o) & 1) || o == playerObject() || int((read(o + 10) << 8) | read(o + 9)) != mapId()) continue;
		Point p(int16_t(read(o + 16, 2)) - 7, int16_t(read(o + 18, 2)) - 7);
		if (std::abs(p.x - pos.x) > radius || std::abs(p.y - pos.y) > radius) continue;
		auto n = coords(p);
		put(n, "local_id", int(read(o + 8)));
		put(n, "graphics_id", int(read(o + 5)));
		put(n, "trainer_type", int(read(o + 7)));
		put(n, "trainer_range", int(read(o + 29)));
		put(n, "facing_direction", int(read(o + 24) & 15));
		npcs.push_back(n);
	}
	uint32_t events = read(MapHeader + 4, 4);
	if (contains(events, 20)) {
		uint32_t count = read(events + 1), warps = read(events + 8, 4);
		if (count <= 64 && contains(warps, count * 8)) for (uint32_t i = 0; i < count; ++i) {
			uint32_t w = warps + i * 8;
			Point p(int16_t(read(w, 2)), int16_t(read(w + 2, 2)));
			if (std::abs(p.x - pos.x) > radius || std::abs(p.y - pos.y) > radius) continue;
			auto e = coords(p); put(e, "warp_id", int(i)); put(e, "destination", Json{{"map_group", int(read(w + 7))}, {"map_num", int(read(w + 6))}, {"warp_id", int(read(w + 5))}}); exits.push_back(e);
		}
	}
	uint32_t c = read(MapHeader + 12, 4);
	if (contains(c, 8)) {
		uint32_t count = read(c, 4), list = read(c + 4, 4);
		if (count <= 16 && contains(list, count * 12)) for (uint32_t i = 0; i < count; ++i) {
			uint32_t a = list + i * 12;
			connections.push_back(Json{{"direction", int(read(a))}, {"offset", int(int32_t(read(a + 4, 4)))}, {"map_group", int(read(a + 8))}, {"map_num", int(read(a + 9))}});
		}
	}
	return {{"available", true}, {"frame", uint64_t(m_frame)}, {"map_id", mapId()}, {"center", coords(pos)}, {"radius", radius}, {"tiles", tiles}, {"npcs", npcs}, {"exits", exits}, {"connections", connections}};
}

Json EmeraldGameState::party() const {
	Json mons = Json::array();
	int count = read(0x020244E9);
	if (count > 6) return {{"available", false}};
	// Logical substructure -> physical block for each personality permutation.
	const int order[24][2] = {{0,1},{0,1},{0,2},{0,3},{0,2},{0,3},{1,0},{1,0},{2,0},{3,0},{2,0},{3,0},{1,2},{1,3},{2,1},{3,1},{2,3},{3,2},{1,2},{1,3},{2,1},{3,1},{2,3},{3,2}};
	for (int i = 0; i < count; ++i) {
		uint32_t a = 0x020244EC + i * 100, personality = read(a, 4), key = personality ^ read(a + 4, 4);
		uint32_t decrypted[12]; uint16_t sum = 0;
		for (int j = 0; j < 12; ++j) { decrypted[j] = read(a + 32 + j * 4, 4) ^ key; sum += uint16_t(decrypted[j]) + uint16_t(decrypted[j] >> 16); }
		if (sum != read(a + 28, 2)) { mons.push_back(Json{{"slot", i}, {"available", false}, {"reason", "Pokemon checksum mismatch"}}); continue; }
		int g = order[personality % 24][0] * 3, attack = order[personality % 24][1] * 3;
		Json moves = Json::array();
		for (int j = 0; j < 4; ++j) moves.push_back(Json{{"id", int((decrypted[attack + j / 2] >> ((j % 2) * 16)) & 0xFFFF)}, {"pp", int((decrypted[attack + 2] >> (j * 8)) & 0xFF)}});
		mons.push_back(Json{{"slot", i}, {"species_id", int(decrypted[g] & 0xFFFF)}, {"held_item_id", int(decrypted[g] >> 16)}, {"level", int(read(a + 84))}, {"hp", int(read(a + 86, 2))}, {"max_hp", int(read(a + 88, 2))}, {"status", uint64_t(read(a + 80, 4))}, {"moves", moves}});
	}
	return {{"available", true}, {"members", mons}};
}

Json EmeraldGameState::inventory() const {
	uint32_t s = read(Save1, 4), key = read(read(Save2, 4) + 0xAC, 4);
	const char* names[] = {"items", "key_items", "poke_balls", "tm_hm", "berries"};
	const int offsets[] = {0x560, 0x5D8, 0x650, 0x690, 0x790}, counts[] = {30, 30, 16, 64, 46};
	Json result = Json::object();
	for (int p = 0; p < 5; ++p) {
		Json entries = Json::array();
		for (int i = 0; i < counts[p]; ++i) {
			uint32_t a = s + offsets[p] + i * 4;
			int item = read(a, 2);
			if (item) entries.push_back(Json{{"item_id", item}, {"quantity", int(read(a + 2, 2) ^ (key & 0xFFFF))}});
		}
		put(result, names[p], entries);
	}
	return result;
}

Json EmeraldGameState::state(bool details) const {
	Json result{{"supported", m_supported}, {"adapter", m_supported ? Json("emerald_en_v1") : Json()}, {"rom_sha1", m_sha1}};
	put(result, "normalized_rom_sha1", m_normalizedSha1);
	if (!m_supported) { put(result, "reason", "Unsupported ROM; raw tools remain available"); return result; }
	put(result, "frame", uint64_t(m_frame));
	put(result, "available", ready());
	put(result, "interaction", interaction());
	put(result, "battle_menu_ready", battleMenuReady());
	put(result, "movement_available", condition("overworld_ready"));
	put(result, "menu", menu());
	put(result, "battle", battle());
	if (details) put(result, "recent_events", m_events);
	if (details && contains(0x020244E9)) put(result, "party", party());
	if (!ready()) return result;
	uint32_t s = read(Save1, 4), o = playerObject(), l = layout();
	put(result, "map", Json{{"id", mapId()}, {"group", int(read(s + 4))}, {"number", int(read(s + 5))}, {"width", int(read(l, 4))}, {"height", int(read(l + 4, 4))}});
	auto p = coords(position());
	const char* facing[] = {"unknown", "south", "north", "west", "east"};
	int face = read(o + 24) & 15;
	put(p, "facing", facing[face <= 4 ? face : 0]);
	put(p, "moving", moving()); put(p, "avatar_flags", int(read(Avatar)));
	put(result, "player", p);
	put(result, "input", Json{{"human_keys", int(m_human)}, {"ai_keys", int(m_ai)}, {"effective_keys", int(m_effective)}});
	if (details) {
		put(result, "inventory", inventory());
		Json flags = Json::array();
		for (int i = 0; i < 0x12C * 8; ++i) if (read(s + 0x1270 + i / 8) & (1 << (i % 8))) flags.push_back(i);
		put(result, "quest_flags", Json{{"format", "set_flag_ids"}, {"ids", flags}});
		Json badges = Json::array();
		const char* badgeNames[] = {"Stone", "Knuckle", "Dynamo", "Heat", "Balance", "Feather", "Mind", "Rain"};
		for (int i = 0; i < 8; ++i) {
			int flag = 0x867 + i;
			if (read(s + 0x1270 + flag / 8) & (1 << (flag % 8))) badges.push_back(badgeNames[i]);
		}
		put(result, "story_progress", Json{{"badges", badges}, {"badge_count", badges.size()}, {"source", "save_block_flags"}});
	}
	return result;
}

bool AIStateAction::startMove(const EmeraldGameState& s, Point target, int maxFrames, bool avoidTrainers) {
	*this = AIStateAction();
	m_avoidTrainers = avoidTrainers;
	m_origin = m_last = s.position(); m_target = target; m_map = s.mapId(); m_start = s.frame(); m_remaining = maxFrames;
	if (!s.condition("overworld_ready") || !(s.state(false).value("player", Json::object()).value("avatar_flags", 0) & 1)) { m_reason = "overworld_not_ready"; return false; }
	if (m_origin == target) { m_reason = "arrived"; m_end = s.frame(); return true; }
	m_path = s.pathTo(target, 16, m_avoidTrainers);
	if (m_path.empty()) { m_reason = "no_path"; m_end = s.frame(); return true; }
	m_pending = true; m_next = takeNext(); m_keys = direction(m_origin, m_next);
	return true;
}

void AIStateAction::startWait(const EmeraldGameState& s, const std::string& condition, int maxFrames, int stableFrames) {
	*this = AIStateAction(); m_wait = true; m_pending = true; m_condition = condition;
	m_start = s.frame(); m_remaining = maxFrames; m_requiredStable = stableFrames;
}

void AIStateAction::stop(const std::string& reason, const EmeraldGameState& s) { m_reason = reason; m_end = s.frame(); m_pending = false; m_keys = 0; }
void AIStateAction::cancel(const std::string& reason, uint64_t frame) { m_reason = reason; m_end = frame; m_pending = false; m_keys = 0; }

bool AIStateAction::tick(const EmeraldGameState& s, unsigned human) {
	if (!m_pending) return false;
	if (human) { stop("human_input", s); return true; }
	if (m_wait) {
		m_stable = s.condition(m_condition) ? m_stable + 1 : 0;
		if (m_stable >= m_requiredStable) { stop("condition_met", s); return true; }
	} else {
		if (!s.ready()) { stop("state_unavailable", s); return true; }
		if (s.mapId() != m_map) { stop("map_transition", s); return true; }
		if (s.interaction() != "overworld") { stop(s.interaction(), s); return true; }
		if (!s.moving() && !s.condition("overworld_ready")) {
			stop("movement_restricted", s);
			return true;
		}
		Point p = s.position();
		if (m_avoidTrainers && m_keys &&
		    (s.trainerRisk(m_next) || ((m_next - p).manhattanLength() == 2 && s.trainerRisk((p + m_next) / 2)))) {
			stop("trainer_sightline", s);
			return true;
		}
		if (m_keys && p == m_last && !s.moving() && !s.canWalk(p, m_next, m_next == m_target)) { stop("blocked", s); return true; }
		if (p != m_last) {
			if (s.moving() && (m_next - m_last).manhattanLength() == 2 && p == (m_last + m_next) / 2) {
				m_keys = 0; m_unchanged = 0;
				if (--m_remaining <= 0) { stop("timeout", s); return true; }
				return false;
			}
			++m_steps; m_unchanged = 0;
			if (p != m_next) { stop("unexpected_movement", s); return true; }
			m_last = p; m_keys = 0;
		} else ++m_unchanged;
		if (!s.moving() && p == m_next) {
			if (p == m_target) { stop("arrived", s); return true; }
			if (m_path.empty()) { stop("route_interrupted", s); return true; }
			m_next = takeNext();
			if (m_avoidTrainers &&
			    (s.trainerRisk(m_next) || ((m_next - p).manhattanLength() == 2 && s.trainerRisk((p + m_next) / 2)))) {
				stop("trainer_sightline", s);
				return true;
			}
			if (!s.canWalk(p, m_next, m_next == m_target)) { stop("blocked", s); return true; }
			m_keys = direction(p, m_next); m_unchanged = 0;
		}
		if (m_unchanged >= 60) { stop("position_unchanged", s); return true; }
	}
	if (--m_remaining <= 0) { stop("timeout", s); return true; }
	return false;
}

Json AIStateAction::result() const {
	return {{"reason", m_reason}, {"completed", m_reason == "arrived" || m_reason == "condition_met"}, {"action_start_frame", uint64_t(m_start)}, {"stop_frame", uint64_t(m_end)}, {"steps_moved", m_steps}, {"position_unchanged", !m_wait && m_origin == m_last}, {"target", coords(m_target)}, {"last_position", coords(m_last)}};
}

Point AIStateAction::takeNext() {
	Point next = m_path.front();
	m_path.erase(m_path.begin());
	return next;
}
