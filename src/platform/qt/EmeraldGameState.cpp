#include "EmeraldGameState.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QHash>
#include <QQueue>
#include <mgba/core/core.h>
#include <algorithm>

using namespace QGBA;

namespace {
constexpr quint32 Save1 = 0x03005D8C, Save2 = 0x03005D90;
constexpr quint32 MapHeader = 0x02037318, Objects = 0x02037350, Avatar = 0x02037590;
constexpr quint32 Main = 0x030022C0, Grid = 0x03005DC0;
QJsonObject coords(QPoint p) { return {{"x", p.x()}, {"y", p.y()}}; }
unsigned direction(QPoint from, QPoint to) {
	if (to.x() > from.x()) return 16;
	if (to.x() < from.x()) return 32;
	if (to.y() < from.y()) return 64;
	return 128;
}
QByteArray backing(mCore* core, const char* name) {
	const mCoreMemoryBlock* blocks = nullptr;
	size_t count = core->listMemoryBlocks(core, &blocks);
	for (size_t i = 0; i < count; ++i) {
		if (QByteArray(blocks[i].internalName) != name) continue;
		size_t size = 0;
		const auto* data = static_cast<const char*>(core->getMemoryBlock(core, blocks[i].id, &size));
		if (data && size <= 0x2000000) return QByteArray(data, int(size));
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

void EmeraldGameState::identify(mCore* core) {
	*this = EmeraldGameState();
	if (core->platform(core) != mPLATFORM_GBA) return;
	m_rom = backing(core, "cart0");
	// Hash the loaded backing store, including patches, rather than the original file.
	m_sha1 = QString::fromLatin1(QCryptographicHash::hash(m_rom, QCryptographicHash::Sha1).toHex());
	m_supported = m_sha1 == romSha1();
	if (!m_supported) m_rom.clear();
}

void EmeraldGameState::capture(mCore* core, quint64 frame, unsigned human, unsigned ai, unsigned effective) {
	if (!m_supported) return;
	m_wram = backing(core, "wram");
	m_iwram = backing(core, "iwram");
	m_frame = frame;
	m_human = human;
	m_ai = ai;
	m_effective = effective;
}

void EmeraldGameState::setSnapshot(const QByteArray& rom, const QByteArray& wram, const QByteArray& iwram, quint64 frame) {
	m_rom = rom; m_wram = wram; m_iwram = iwram; m_frame = frame;
}

bool EmeraldGameState::contains(quint32 a, quint32 length) const {
	return (a >= 0x02000000 && quint64(a) + length <= 0x02000000ULL + m_wram.size()) ||
		(a >= 0x03000000 && quint64(a) + length <= 0x03000000ULL + m_iwram.size()) ||
		(a >= 0x08000000 && quint64(a) + length <= 0x08000000ULL + m_rom.size());
}

quint32 EmeraldGameState::read(quint32 a, int length) const {
	if (!contains(a, length)) return 0;
	const QByteArray* bytes = &m_rom;
	quint32 base = 0x08000000;
	if (a < 0x03000000) { bytes = &m_wram; base = 0x02000000; }
	else if (a < 0x08000000) { bytes = &m_iwram; base = 0x03000000; }
	quint32 value = 0;
	for (int i = 0; i < length; ++i) value |= quint32(quint8(bytes->at(int(a - base) + i))) << (i * 8);
	return value;
}

quint32 EmeraldGameState::playerObject() const {
	int index = read(Avatar + 5);
	if (index >= 16) return 0;
	quint32 a = Objects + index * 36;
	return (read(a) & 1) && (read(a + 2) & 1) ? a : 0;
}

quint32 EmeraldGameState::layout() const {
	quint32 p = read(MapHeader, 4);
	return contains(p, 24) && read(p, 4) > 0 && read(p, 4) <= 512 && read(p + 4, 4) > 0 && read(p + 4, 4) <= 512 ? p : 0;
}

bool EmeraldGameState::ready() const {
	// SaveBlock1 and SaveBlock2 must live wholly within EWRAM.
	quint32 s1 = read(Save1, 4), s2 = read(Save2, 4);
	return s1 >= 0x02000000 && quint64(s1) + 0x3D88 <= 0x02040000 &&
		s2 >= 0x02000000 && quint64(s2) + 0xF2C <= 0x02040000 && contains(s1, 0x3D88) && contains(s2, 0xF2C) && layout() && playerObject();
}

QPoint EmeraldGameState::position() const {
	quint32 p = playerObject();
	return p ? QPoint(qint16(read(p + 16, 2)) - 7, qint16(read(p + 18, 2)) - 7) : QPoint(-1, -1);
}
int EmeraldGameState::mapId() const { quint32 s = read(Save1, 4); return ready() ? int((read(s + 4) << 8) | read(s + 5)) : -1; }
bool EmeraldGameState::moving() const { return read(Avatar + 3) == 1 || ((read(playerObject()) & 0x40) && !(read(playerObject()) & 0x80)); }

QString EmeraldGameState::interaction() const {
	quint32 callback = read(Main + 4, 4) & ~1U;
	if ((read(Main + 0x439) & 2) || callback == 0x08038420 || callback == 0x08036760 || callback == 0x080367D4) return "battle";
	if (!ready()) return "unavailable";
	if (callback != 0x08085E5C) return "transition_or_menu";
	if (read(0x020375BC)) return "dialogue";
	for (int i = 0; i < 16; ++i) {
		quint32 task = 0x03005E00 + i * 40;
		if (read(task + 4) && (read(task, 4) & ~1U) == 0x0809FA34) return "menu";
	}
	if (read(0x03000F2C) || read(Avatar + 6) || (read(playerObject() + 1) & 1)) return "scripted";
	return "overworld";
}

bool EmeraldGameState::battleMenuReady() const {
	if (interaction() != "battle") return false;
	for (int i : {0, 2}) if ((read(0x03005D60 + i * 4, 4) & ~1U) == 0x08057588) return true;
	return false;
}

bool EmeraldGameState::condition(const QString& name) const {
	if (name == "battle_menu_ready") return battleMenuReady();
	if (name == "dialogue") return interaction() == "dialogue";
	if (name == "overworld_ready" || name == "map_transition_complete")
		return ready() && interaction() == "overworld" && !moving() && !(read(0x02037FD4 + 7) & 0x80);
	return false;
}

int EmeraldGameState::tile(QPoint p) const {
	quint32 l = layout();
	if (!l || p.x() < 0 || p.y() < 0 || p.x() >= int(read(l, 4)) || p.y() >= int(read(l + 4, 4))) return -1;
	quint32 w = read(Grid, 4), h = read(Grid + 4, 4), base = read(Grid + 8, 4);
	if (w != read(l, 4) + 15 || h != read(l + 4, 4) + 14 || w * h > 0x2800 || !contains(base, w * h * 2)) return -1;
	quint32 address = base + ((p.y() + 7) * w + p.x() + 7) * 2;
	return int(read(address, 2));
}

int EmeraldGameState::behavior(int metatile) const {
	quint32 l = layout();
	if (!l) return -1;
	quint32 tileset = read(l + (metatile < 512 ? 16 : 20), 4);
	if (!contains(tileset, 24)) return -1;
	quint32 attr = read(tileset + 16, 4) + (metatile % 512) * 2;
	return contains(attr, 2) ? int(read(attr, 2) & 0xFF) : -1;
}

bool EmeraldGameState::occupied(QPoint point) const {
	for (int i = 0; i < 16; ++i) {
		quint32 o = Objects + i * 36;
		if (!(read(o) & 1) || o == playerObject() || int((read(o + 10) << 8) | read(o + 9)) != mapId()) continue;
		for (int offset : {16, 20}) if (point == QPoint(qint16(read(o + offset, 2)) - 7, qint16(read(o + offset + 2, 2)) - 7)) return true;
	}
	return false;
}

QJsonObject EmeraldGameState::tileState(QPoint p) const {
	int t = tile(p), b = t >= 0 ? behavior(t & 0x3FF) : -1;
	auto result = coords(p);
	result.insert("known", t >= 0 && b >= 0);
	if (t < 0) return result;
	result.insert("metatile_id", t & 0x3FF);
	result.insert("collision", (t >> 10) & 3);
	result.insert("elevation", t >> 12);
	result.insert("behavior", b);
	result.insert("occupied", occupied(p));
	result.insert("special_traversal", special(b));
	if (b >= 0x38 && b <= 0x3F) {
		const char* ledges[] = {"east", "west", "north", "south", "northeast", "northwest", "southeast", "southwest"};
		result.insert("one_way_ledge", ledges[b - 0x38]);
	}
	if (b == 0x61) result.insert("ladder", true);
	return result;
}

bool EmeraldGameState::canWalk(QPoint from, QPoint to, bool destination) const {
	int a = tile(from), b = tile(to);
	if (a < 0 || b < 0 || (b & 0xC00) || occupied(to) || special(behavior(a & 0x3FF)) || special(behavior(b & 0x3FF))) return false;
	int e1 = a >> 12, e2 = b >> 12;
	if (e1 && e2 && e1 != 15 && e2 != 15 && e1 != e2) return false;
	int beh = behavior(b & 0x3FF);
	// Exits can be requested as the destination but are never crossed incidentally.
	if (!destination && beh >= 0x60 && beh <= 0x6F) return false;
	return true;
}

QVector<QPoint> EmeraldGameState::pathTo(QPoint target, int radius) const {
	const QPoint origin = position();
	if (origin == target) return {};
	QQueue<QPoint> queue; queue.enqueue(origin);
	QHash<int, QPoint> parent;
	auto key = [](QPoint p) { return p.y() * 1024 + p.x(); };
	parent.insert(key(origin), origin);
	while (!queue.isEmpty()) {
		QPoint p = queue.dequeue();
		for (QPoint d : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
			QPoint next = p + d;
			if (qAbs(next.x() - origin.x()) > radius || qAbs(next.y() - origin.y()) > radius || parent.contains(key(next)) || !canWalk(p, next, next == target)) continue;
			parent.insert(key(next), p);
			if (next == target) {
				QVector<QPoint> path;
				for (QPoint n = next; n != origin; n = parent.value(key(n))) path.prepend(n);
				return path;
			}
			queue.enqueue(next);
		}
	}
	return {};
}

QJsonObject EmeraldGameState::localMap(int radius) const {
	if (!ready()) return {{"available", false}, {"reason", "No initialized map snapshot"}};
	QJsonArray tiles, npcs, exits, connections;
	QPoint pos = position();
	for (int y = pos.y() - radius; y <= pos.y() + radius; ++y)
		for (int x = pos.x() - radius; x <= pos.x() + radius; ++x) tiles.append(tileState({x, y}));
	for (int i = 0; i < 16; ++i) {
		quint32 o = Objects + i * 36;
		if (!(read(o) & 1) || o == playerObject() || int((read(o + 10) << 8) | read(o + 9)) != mapId()) continue;
		QPoint p(qint16(read(o + 16, 2)) - 7, qint16(read(o + 18, 2)) - 7);
		if (qAbs(p.x() - pos.x()) > radius || qAbs(p.y() - pos.y()) > radius) continue;
		auto n = coords(p); n.insert("local_id", int(read(o + 8))); n.insert("graphics_id", int(read(o + 5))); npcs.append(n);
	}
	quint32 events = read(MapHeader + 4, 4);
	if (contains(events, 20)) {
		quint32 count = read(events + 1), warps = read(events + 8, 4);
		if (count <= 64 && contains(warps, count * 8)) for (quint32 i = 0; i < count; ++i) {
			quint32 w = warps + i * 8;
			QPoint p(qint16(read(w, 2)), qint16(read(w + 2, 2)));
			if (qAbs(p.x() - pos.x()) > radius || qAbs(p.y() - pos.y()) > radius) continue;
			auto e = coords(p); e.insert("warp_id", int(i)); e.insert("destination", QJsonObject{{"map_group", int(read(w + 7))}, {"map_num", int(read(w + 6))}, {"warp_id", int(read(w + 5))}}); exits.append(e);
		}
	}
	quint32 c = read(MapHeader + 12, 4);
	if (contains(c, 8)) {
		quint32 count = read(c, 4), list = read(c + 4, 4);
		if (count <= 16 && contains(list, count * 12)) for (quint32 i = 0; i < count; ++i) {
			quint32 a = list + i * 12;
			connections.append(QJsonObject{{"direction", int(read(a))}, {"offset", int(qint32(read(a + 4, 4)))}, {"map_group", int(read(a + 8))}, {"map_num", int(read(a + 9))}});
		}
	}
	return {{"available", true}, {"frame", double(m_frame)}, {"map_id", mapId()}, {"center", coords(pos)}, {"radius", radius}, {"tiles", tiles}, {"npcs", npcs}, {"exits", exits}, {"connections", connections}};
}

QJsonObject EmeraldGameState::party() const {
	QJsonArray mons;
	int count = read(0x020244E9);
	if (count > 6) return {{"available", false}};
	// Logical substructure -> physical block for each personality permutation.
	const int order[24][2] = {{0,1},{0,1},{0,2},{0,3},{0,2},{0,3},{1,0},{1,0},{2,0},{3,0},{2,0},{3,0},{1,2},{1,3},{2,1},{3,1},{2,3},{3,2},{1,2},{1,3},{2,1},{3,1},{2,3},{3,2}};
	for (int i = 0; i < count; ++i) {
		quint32 a = 0x020244EC + i * 100, personality = read(a, 4), key = personality ^ read(a + 4, 4);
		quint32 decrypted[12]; quint16 sum = 0;
		for (int j = 0; j < 12; ++j) { decrypted[j] = read(a + 32 + j * 4, 4) ^ key; sum += quint16(decrypted[j]) + quint16(decrypted[j] >> 16); }
		if (sum != read(a + 28, 2)) { mons.append(QJsonObject{{"slot", i}, {"available", false}, {"reason", "Pokemon checksum mismatch"}}); continue; }
		int g = order[personality % 24][0] * 3, attack = order[personality % 24][1] * 3;
		QJsonArray moves;
		for (int j = 0; j < 4; ++j) moves.append(QJsonObject{{"id", int((decrypted[attack + j / 2] >> ((j % 2) * 16)) & 0xFFFF)}, {"pp", int((decrypted[attack + 2] >> (j * 8)) & 0xFF)}});
		mons.append(QJsonObject{{"slot", i}, {"species_id", int(decrypted[g] & 0xFFFF)}, {"held_item_id", int(decrypted[g] >> 16)}, {"level", int(read(a + 84))}, {"hp", int(read(a + 86, 2))}, {"max_hp", int(read(a + 88, 2))}, {"status", double(read(a + 80, 4))}, {"moves", moves}});
	}
	return {{"available", true}, {"members", mons}};
}

QJsonObject EmeraldGameState::inventory() const {
	quint32 s = read(Save1, 4), key = read(read(Save2, 4) + 0xAC, 4);
	const char* names[] = {"items", "key_items", "poke_balls", "tm_hm", "berries"};
	const int offsets[] = {0x560, 0x5D8, 0x650, 0x690, 0x790}, counts[] = {30, 30, 16, 64, 46};
	QJsonObject result;
	for (int p = 0; p < 5; ++p) {
		QJsonArray entries;
		for (int i = 0; i < counts[p]; ++i) {
			quint32 a = s + offsets[p] + i * 4;
			int item = read(a, 2);
			if (item) entries.append(QJsonObject{{"item_id", item}, {"quantity", int(read(a + 2, 2) ^ (key & 0xFFFF))}});
		}
		result.insert(names[p], entries);
	}
	return result;
}

QJsonObject EmeraldGameState::state(bool details) const {
	QJsonObject result{{"supported", m_supported}, {"adapter", m_supported ? QJsonValue("emerald_en_v1") : QJsonValue()}, {"rom_sha1", m_sha1}};
	if (!m_supported) { result.insert("reason", "Unsupported ROM; raw tools remain available"); return result; }
	result.insert("frame", double(m_frame));
	result.insert("available", ready());
	result.insert("interaction", interaction());
	result.insert("battle_menu_ready", battleMenuReady());
	if (details && contains(0x020244E9)) result.insert("party", party());
	if (!ready()) return result;
	quint32 s = read(Save1, 4), o = playerObject(), l = layout();
	result.insert("map", QJsonObject{{"id", mapId()}, {"group", int(read(s + 4))}, {"number", int(read(s + 5))}, {"width", int(read(l, 4))}, {"height", int(read(l + 4, 4))}});
	auto p = coords(position());
	const char* facing[] = {"unknown", "south", "north", "west", "east"};
	int face = read(o + 24) & 15;
	p.insert("facing", facing[face <= 4 ? face : 0]);
	p.insert("moving", moving()); p.insert("avatar_flags", int(read(Avatar)));
	result.insert("player", p);
	result.insert("input", QJsonObject{{"human_keys", int(m_human)}, {"ai_keys", int(m_ai)}, {"effective_keys", int(m_effective)}});
	if (details) {
		result.insert("inventory", inventory());
		QJsonArray flags;
		for (int i = 0; i < 0x12C * 8; ++i) if (read(s + 0x1270 + i / 8) & (1 << (i % 8))) flags.append(i);
		result.insert("quest_flags", QJsonObject{{"format", "set_flag_ids"}, {"ids", flags}});
	}
	return result;
}

bool AIStateAction::startMove(const EmeraldGameState& s, QPoint target, int maxFrames) {
	*this = AIStateAction();
	m_origin = m_last = s.position(); m_target = target; m_map = s.mapId(); m_start = s.frame(); m_remaining = maxFrames;
	if (!s.condition("overworld_ready") || !(s.state(false).value("player").toObject().value("avatar_flags").toInt() & 1)) { m_reason = "overworld_not_ready"; return false; }
	if (m_origin == target) { m_reason = "arrived"; m_end = s.frame(); return true; }
	m_path = s.pathTo(target);
	if (m_path.isEmpty()) { m_reason = "no_path"; m_end = s.frame(); return true; }
	m_pending = true; m_next = m_path.takeFirst(); m_keys = direction(m_origin, m_next);
	return true;
}

void AIStateAction::startWait(const EmeraldGameState& s, const QString& condition, int maxFrames, int stableFrames) {
	*this = AIStateAction(); m_wait = true; m_pending = true; m_condition = condition;
	m_start = s.frame(); m_remaining = maxFrames; m_requiredStable = stableFrames;
}

void AIStateAction::stop(const QString& reason, const EmeraldGameState& s) { m_reason = reason; m_end = s.frame(); m_pending = false; m_keys = 0; }
void AIStateAction::cancel(const QString& reason, quint64 frame) { m_reason = reason; m_end = frame; m_pending = false; m_keys = 0; }

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
		QPoint p = s.position();
		if (m_keys && p == m_last && !s.moving() && !s.canWalk(p, m_next, m_next == m_target)) { stop("blocked", s); return true; }
		if (p != m_last) {
			++m_steps; m_unchanged = 0;
			if (p != m_next) { stop("unexpected_movement", s); return true; }
			m_last = p; m_keys = 0;
		} else ++m_unchanged;
		if (!s.moving() && p == m_next) {
			if (p == m_target) { stop("arrived", s); return true; }
			if (m_path.isEmpty()) { stop("route_interrupted", s); return true; }
			m_next = m_path.takeFirst();
			if (!s.canWalk(p, m_next, m_next == m_target)) { stop("blocked", s); return true; }
			m_keys = direction(p, m_next); m_unchanged = 0;
		}
		if (m_unchanged >= 60) { stop("position_unchanged", s); return true; }
	}
	if (--m_remaining <= 0) { stop("timeout", s); return true; }
	return false;
}

QJsonObject AIStateAction::result() const {
	return {{"reason", m_reason}, {"completed", m_reason == "arrived" || m_reason == "condition_met"}, {"action_start_frame", double(m_start)}, {"stop_frame", double(m_end)}, {"steps_moved", m_steps}, {"position_unchanged", !m_wait && m_origin == m_last}, {"target", coords(m_target)}, {"last_position", coords(m_last)}};
}
