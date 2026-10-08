#include "../AIGameplay.h"

#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed line " << __LINE__ << ": " << #condition << '\n'; std::abort(); } } while (0)

int main() {
	using QGBA::AIGameplay;
	constexpr unsigned A = 1, B = 2, Right = 16, Left = 32, Up = 64, Down = 128;
	CHECK(AIGameplay::mergeKeys(Left | A, Right | B) == (Left | A | B));
	CHECK(AIGameplay::mergeKeys(Right, Left) == Right);
	CHECK(AIGameplay::mergeKeys(Up, Down) == Up);
	CHECK(AIGameplay::mergeKeys(Down, Up) == Down);
	CHECK(AIGameplay::mergeKeys(Left | Down, Right | Up | A) == (Left | Down | A));
	CHECK(AIGameplay::mergeKeys(A, A) == A);
	CHECK(AIGameplay::mergeKeys(0, A | Up) == (A | Up));
	CHECK(AIGameplay::mergeKeys(Left | A, 0) == (Left | A));

	AIGameplay game;
	CHECK(!game.start({}, 0));
	CHECK(!game.start({{A, 0}}, 0));
	CHECK(!game.start({{A, 601}}, 0));
	CHECK(!game.start({{0x400, 1}}, 0));
	CHECK(!game.start({{A, 600}, {B, 1}}, 0));
	CHECK(!game.start(std::vector<AIGameplay::Step>(201, {A, 1}), 0));
	CHECK(game.start({{Right, 2}, {A, 1}, {0, 2}}, 100));
	CHECK(game.startFrame() == 100 && game.endFrame() == 105);
	CHECK(!game.start({{B, 1}}, 100));
	CHECK(game.keys() == Right && game.completed() == 0);
	CHECK(!game.finishFrame());
	CHECK(game.keys() == Right && game.completed() == 0);
	CHECK(!game.finishFrame());
	CHECK(game.keys() == A && game.completed() == 1);
	CHECK(!game.finishFrame());
	CHECK(game.keys() == 0 && game.pending() && game.completed() == 2);
	CHECK(!game.finishFrame());
	CHECK(game.finishFrame());
	CHECK(!game.pending() && game.keys() == 0 && game.completed() == 3);
	CHECK(!game.finishFrame());
	CHECK(AIGameplay::mergeKeys(Left | A, game.keys()) == (Left | A));

	CHECK(game.start({{A, 1}, {B, 5}, {Right, 2}}, 200));
	CHECK(!game.finishFrame());
	CHECK(!game.finishFrame());
	game.cancel();
	CHECK(game.completed() == 1 && !game.pending() && game.keys() == 0);
	for (int i = 0; i < 10; ++i) CHECK(!game.finishFrame());
	CHECK(game.completed() == 1);
	CHECK(game.start({{B, 600}}, 300));
	for (int i = 0; i < 599; ++i) CHECK(!game.finishFrame());
	CHECK(game.finishFrame() && game.completed() == 1 && game.endFrame() == 900);
	std::vector<AIGameplay::Step> longSequence;
	for (int i = 0; i < 200; ++i) longSequence.push_back({i % 2 ? Left : Right, 3});
	CHECK(game.start(longSequence, 1000));
	for (int i = 0; i < 200; ++i) {
		CHECK(game.keys() == longSequence[i].keys && game.completed() == std::size_t(i));
		CHECK(!game.finishFrame());
		CHECK(!game.finishFrame());
		CHECK(game.finishFrame() == (i == 199));
	}
	CHECK(!game.pending() && game.keys() == 0 && game.completed() == 200 && game.endFrame() == 1600);
	std::cout << "PASS: shared directional priority, contiguous exact frame steps, limits, cancellation, input release, and restart\n";
}
