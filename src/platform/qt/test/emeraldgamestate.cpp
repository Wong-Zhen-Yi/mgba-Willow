#include "../EmeraldGameState.h"
#include <QJsonArray>
#include <mgba/core/core.h>
#include <cstdlib>
#include <iostream>
#include <algorithm>

#define CHECK(c) do { if (!(c)) { std::cerr << "Failed line " << __LINE__ << ": " << #c << '\n'; std::abort(); } } while (0)

namespace QGBA {
struct EmeraldGameStateFixture {
	QByteArray rom = QByteArray(0x1000, '\0'), ram = QByteArray(0x40000, '\0'), internal = QByteArray(0x8000, '\0');
	EmeraldGameState game;
	quint64 frame = 10;
	void put(quint32 a, quint32 value, int len = 4) {
		QByteArray* bytes = a >= 0x08000000 ? &rom : a >= 0x03000000 ? &internal : &ram;
		quint32 base = a >= 0x08000000 ? 0x08000000 : a >= 0x03000000 ? 0x03000000 : 0x02000000;
		for (int i = 0; i < len; ++i) (*bytes)[int(a - base) + i] = char(value >> (i * 8));
	}
	void tile(int x, int y, int value) { put(0x02001000 + ((y + 7) * 23 + x + 7) * 2, value, 2); }
	void position(int x, int y) { put(0x02037360, x + 7, 2); put(0x02037362, y + 7, 2); }
	void sync() { game.setSnapshot(rom, ram, internal, frame++); }
	EmeraldGameStateFixture() {
		game.m_supported = true; game.m_sha1 = EmeraldGameState::romSha1();
		put(0x03005D8C, 0x02020000); put(0x03005D90, 0x02028000);
		put(0x02020004, 0x0201, 2);
		put(0x030022C4, 0x08085E5D);
		put(0x02037318, 0x08000100); put(0x08000100, 8); put(0x08000104, 8);
		put(0x08000110, 0x08000200); put(0x08000210, 0x08000400);
		put(0x03005DC0, 23); put(0x03005DC4, 22); put(0x03005DC8, 0x02001000);
		put(0x02037350, 1, 1); put(0x02037352, 1, 1); put(0x02037368, 4, 1);
		put(0x02037590, 1, 1); position(1, 1);
		sync();
	}
};
}

int main() {
	using namespace QGBA;
	EmeraldGameStateFixture f;
	CHECK(f.game.ready()); CHECK(f.game.position() == QPoint(1, 1)); CHECK(f.game.mapId() == 0x0102);
	CHECK(f.game.state().value("player").toObject().value("facing") == "east");
	CHECK(f.game.condition("overworld_ready"));
	// Block the direct route; BFS must go around it using the live grid.
	f.tile(2, 1, 0x400); f.sync();
	CHECK(!f.game.canWalk({1,1}, {2,1}));
	auto route = f.game.pathTo({3,1}); CHECK(route.size() == 4); CHECK(route.first() != QPoint(2,1));
	// Dynamic save pointers are resolved from the same copied frame.
	f.put(0x02021004, 0x0403, 2); f.put(0x03005D8C, 0x02021000);
	CHECK(f.game.mapId() == 0x0102); // Live mutations cannot tear the captured frame.
	CHECK(f.game.state().value("frame") == f.game.localMap(2).value("frame"));
	f.sync();
	CHECK(f.game.mapId() == 0x0304);
	f.put(0x03005D8C, 0xFFFFFFFF); f.sync(); CHECK(!f.game.ready());
	f.put(0x03005D8C, 0x02020000); f.sync();
	// Bad collision pointers must fail closed, including integer overflow.
	f.put(0x03005DC8, 0xFFFFFFFE); f.sync(); CHECK(f.game.pathTo({3,1}).isEmpty());
	f.put(0x03005DC8, 0x02001000); f.tile(2,1,0); f.sync();
	AIStateAction move;
	CHECK(move.startMove(f.game, {3,1}, 100)); CHECK(move.keys() == 16);
	f.position(2,1); f.put(0x02037593,1,1); f.sync();
	CHECK(!move.tick(f.game,0)); CHECK(move.keys() == 0);
	f.put(0x02037593,0,1); f.sync(); CHECK(!move.tick(f.game,0)); CHECK(move.keys() == 16);
	f.position(3,1); f.sync(); CHECK(move.tick(f.game,0)); CHECK(move.result().value("completed").toBool()); CHECK(move.keys() == 0);
	// Encounters, dialogue, script locks and human keys release immediately.
	for (int mode = 0; mode < 4; ++mode) {
		f.position(1,1); f.put(0x030026F9,0,1); f.put(0x020375BC,0,1); f.put(0x03000F2C,0,1); f.sync();
		CHECK(move.startMove(f.game,{3,1},100));
		if (mode == 0) f.put(0x030026F9,2,1);
		if (mode == 1) f.put(0x020375BC,1,1);
		if (mode == 2) f.put(0x03000F2C,1,1);
		f.sync(); CHECK(move.tick(f.game,mode == 3 ? 1 : 0)); CHECK(move.keys() == 0); CHECK(!move.result().value("completed").toBool());
	}
	f.put(0x03000F2C,0,1); f.sync(); CHECK(move.startMove(f.game,{3,1},100));
	for (int i=0; i<59; ++i) { f.sync(); CHECK(!move.tick(f.game,0)); }
	f.sync(); CHECK(move.tick(f.game,0)); CHECK(move.result().value("reason") == "position_unchanged");
	CHECK(move.startMove(f.game,{3,1},100)); f.put(0x02020005,3,1); f.sync();
	CHECK(move.tick(f.game,0)); CHECK(move.result().value("reason") == "map_transition");
	AIStateAction wait; wait.startWait(f.game,"overworld_ready",10,2);
	f.sync(); CHECK(!wait.tick(f.game,0)); f.sync(); CHECK(wait.tick(f.game,0)); CHECK(wait.result().value("reason") == "condition_met");
	f.put(0x030026F9,2,1); f.put(0x03005D60,0x08057589); f.sync(); CHECK(f.game.battleMenuReady());
	f.put(0x03005D60,0); f.sync(); wait.startWait(f.game,"battle_menu_ready",2,1);
	f.sync(); CHECK(!wait.tick(f.game,0)); f.sync(); CHECK(wait.tick(f.game,0)); CHECK(wait.result().value("reason") == "timeout");
	// Bag quantities use the captured SaveBlock2 encryption key.
	f.put(0x020280AC,0x12345678); f.put(0x02020560,13,2); f.put(0x02020562,5 ^ 0x5678,2); f.sync();
	CHECK(f.game.state().value("inventory").toObject().value("items").toArray().first().toObject().value("quantity") == 5);
	f.put(0x02021270,0x10,1); f.sync();
	CHECK(f.game.state().value("quest_flags").toObject().value("ids").toArray().contains(4));
	// A decrypted Pokemon is accepted only when all secure blocks checksum.
	f.put(0x020244E9,1,1); f.put(0x020244EC,0); f.put(0x020244F0,0);
	f.put(0x0202450C,25 | (7 << 16)); f.put(0x02024518,33); f.put(0x02024520,10);
	f.put(0x02024508,25+7+33+10,2); f.put(0x02024542,20,2); f.put(0x02024544,30,2); f.sync();
	auto mon = f.game.state().value("party").toObject().value("members").toArray().first().toObject();
	CHECK(mon.value("species_id") == 25); CHECK(mon.value("hp") == 20); CHECK(mon.value("moves").toArray().first().toObject().value("id") == 33);
	f.put(0x02024508,0,2); f.sync();
	CHECK(!f.game.state().value("party").toObject().value("members").toArray().first().toObject().value("available").toBool(true));
	int physical[4] = {0,1,2,3}, personality = 0;
	do {
		const quint32 blocks[4][3] = {{25 | (7 << 16), 0, 0}, {33, 0, 10}, {0,0,0}, {0,0,0}};
		quint32 key = quint32(personality) ^ 0x12345678;
		f.put(0x020244EC,personality); f.put(0x020244F0,0x12345678); f.put(0x02024508,75,2);
		for (int block=0; block<4; ++block) for (int word=0; word<3; ++word)
			f.put(0x0202450C + block*12 + word*4, blocks[physical[block]][word] ^ key);
		f.sync();
		auto decoded = f.game.state().value("party").toObject().value("members").toArray().first().toObject();
		CHECK(decoded.value("species_id") == 25); CHECK(decoded.value("held_item_id") == 7);
		CHECK(decoded.value("moves").toArray().first().toObject().value("id") == 33);
		CHECK(decoded.value("moves").toArray().first().toObject().value("pp") == 10);
		++personality;
	} while (std::next_permutation(physical, physical+4));
	CHECK(personality == 24);
	// Live objects block both their current and reserved previous positions.
	f.put(0x030026F9,0,1); f.put(0x02020005,2,1); f.position(1,1);
	quint32 npc = 0x02037350 + 36;
	f.put(npc,1,1); f.put(npc+9,2,1); f.put(npc+10,1,1);
	f.put(npc+16,9,2); f.put(npc+18,8,2); f.put(npc+20,9,2); f.put(npc+22,8,2); f.sync();
	CHECK(!f.game.canWalk({1,1},{2,1}));
	CHECK(f.game.localMap(2).value("npcs").toArray().size() == 1);
	f.put(npc,0,1); f.sync(); CHECK(move.startMove(f.game,{3,1},100));
	f.tile(2,1,0x400); f.sync(); CHECK(move.tick(f.game,0)); CHECK(move.result().value("reason") == "blocked");
	f.put(0x08000402,0x38,2); f.tile(2,1,1); f.put(0x08000404,0x61,2); f.tile(1,2,2);
	f.put(0x0203731C,0x08000600); f.put(0x08000601,1,1); f.put(0x08000608,0x08000700);
	f.put(0x08000700,1,2); f.put(0x08000702,2,2); f.put(0x08000706,4,1); f.put(0x08000707,3,1); f.sync();
	bool ledge = false, ladder = false;
	for (const auto& value : f.game.localMap(2).value("tiles").toArray()) {
		auto t = value.toObject(); ledge |= t.value("one_way_ledge") == "east"; ladder |= t.value("ladder").toBool();
	}
	CHECK(ledge && ladder); CHECK(!f.game.canWalk({1,1},{2,1}));
	auto exit = f.game.localMap(2).value("exits").toArray().first().toObject();
	CHECK(exit.value("x") == 1); CHECK(exit.value("destination").toObject().value("map_group") == 3);
	// Wrong ROM checksum never enables the decoder, even on the GBA platform.
	mCore core{};
	core.platform = [](const mCore*) { return mPLATFORM_GBA; };
	core.listMemoryBlocks = [](const mCore*, const mCoreMemoryBlock**) -> size_t { return 0; };
	EmeraldGameState unsupported; unsupported.identify(&core);
	CHECK(!unsupported.supported()); CHECK(!unsupported.state().value("supported").toBool());
	std::cout << "PASS: frame snapshots, dynamic pointers, live collision routing, interruption, timeout, inventory and party checksums\n";
}
