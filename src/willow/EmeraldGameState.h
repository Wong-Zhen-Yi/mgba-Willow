#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

struct mCore;

namespace Willow {

using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;

struct Point {
	int x = 0;
	int y = 0;

	Point() = default;
	Point(int x, int y) : x(x), y(y) {}
	bool operator==(const Point& o) const { return x == o.x && y == o.y; }
	bool operator!=(const Point& o) const { return !(*this == o); }
	Point operator+(const Point& o) const { return { x + o.x, y + o.y }; }
	Point operator-(const Point& o) const { return { x - o.x, y - o.y }; }
	Point operator/(int d) const { return { x / d, y / d }; }
	Point& operator+=(const Point& o) {
		x += o.x;
		y += o.y;
		return *this;
	}
	int manhattanLength() const { return std::abs(x) + std::abs(y); }
};

// Read-only decoder for explicitly verified English Emerald ROM layouts.
// RAM is copied at frame end. All pointers are resolved inside that copy.
class EmeraldGameState {
public:
	static const char* romSha1();
	void identify(mCore* core);
	void capture(mCore* core, uint64_t frame, unsigned human, unsigned ai, unsigned effective);
	bool supported() const { return m_supported; }
	bool hasSnapshot() const { return m_wram.size() == 0x40000 && m_iwram.size() == 0x8000; }
	bool ready() const;
	uint64_t frame() const { return m_frame; }
	Json state(bool details = true) const;
	Json localMap(int radius) const;
	Point position() const;
	int mapId() const;
	std::string interaction() const;
	bool moving() const;
	bool battleMenuReady() const;
	bool condition(const std::string& name) const;
	bool canWalk(Point from, Point to, bool destination = false) const;
	std::vector<Point> pathTo(Point target, int radius = 16, bool avoidTrainers = true) const;
	bool trainerRisk(Point point) const;

	// Also used by fixture tests; production data comes only from backing RAM/ROM.
	void setSnapshot(const Bytes& rom, const Bytes& wram, const Bytes& iwram, uint64_t frame);

private:
	friend struct EmeraldGameStateFixture;
	static bool supportsSha1(const std::string& sha1);
	bool contains(uint32_t address, uint32_t length = 1) const;
	uint32_t read(uint32_t address, int length = 1) const;
	uint32_t playerObject() const;
	uint32_t layout() const;
	int tile(Point point) const;
	int behavior(int metatile) const;
	bool occupied(Point point) const;
	Json tileState(Point point) const;
	Json party() const;
	Json inventory() const;
	Json battle() const;
	Json menu() const;
	std::string text(uint32_t address, int limit) const;
	void recordEvents();
	Json m_events = Json::array();
	std::string m_lastMessage;
	bool m_saveConfirmed = false;
	Bytes m_rom, m_wram, m_iwram;
	std::string m_sha1, m_normalizedSha1;
	bool m_supported = false;
	uint64_t m_frame = 0;
	unsigned m_human = 0, m_ai = 0, m_effective = 0;
};

// Frame-thread state machine. No timer or IPC round trip participates in movement.
class AIStateAction {
public:
	bool startMove(const EmeraldGameState& state, Point target, int maxFrames, bool avoidTrainers = true);
	void startWait(const EmeraldGameState& state, const std::string& condition, int maxFrames, int stableFrames);
	bool tick(const EmeraldGameState& state, unsigned human);
	void cancel(const std::string& reason, uint64_t frame = 0);
	bool pending() const { return m_pending; }
	unsigned keys() const { return m_pending ? m_keys : 0; }
	Json result() const;

private:
	void stop(const std::string& reason, const EmeraldGameState& state);
	Point takeNext();
	bool m_pending = false, m_wait = false;
	bool m_avoidTrainers = true;
	unsigned m_keys = 0;
	int m_remaining = 0, m_stable = 0, m_requiredStable = 1, m_unchanged = 0, m_steps = 0;
	uint64_t m_start = 0, m_end = 0;
	int m_map = -1;
	Point m_origin, m_last, m_target, m_next;
	std::vector<Point> m_path;
	std::string m_condition, m_reason;
};

}
