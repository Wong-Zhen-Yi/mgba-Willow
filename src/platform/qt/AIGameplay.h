#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace QGBA {

// The emulation thread owns this timeline; GUI access requires an Interrupter.
class AIGameplay {
public:
	struct Step { unsigned keys; int frames; };

	bool start(const std::vector<Step>& steps, uint64_t frame) {
		if (pending() || steps.empty() || steps.size() > 32) return false;
		int total = 0;
		for (const auto& step : steps) {
			if (step.frames < 1 || step.frames > 600 || step.keys > 0x3FF) return false;
			total += step.frames;
		}
		if (total > 600) return false;
		m_steps = steps;
		m_completed = 0;
		m_remaining = steps.front().frames;
		m_start = frame;
		m_end = frame + total;
		return true;
	}

	// Called after each completed emulated frame. True only on final completion.
	bool finishFrame() {
		if (!pending() || --m_remaining) return false;
		++m_completed;
		if (m_completed == m_steps.size()) return true;
		m_remaining = m_steps[m_completed].frames;
		return false;
	}
	void cancel() { m_remaining = 0; }
	bool pending() const { return m_remaining > 0; }
	unsigned keys() const { return pending() ? m_steps[m_completed].keys : 0; }
	std::size_t completed() const { return m_completed; }
	uint64_t startFrame() const { return m_start; }
	uint64_t endFrame() const { return m_end; }

	static unsigned mergeKeys(unsigned human, unsigned ai) {
		// Human direction wins on each axis, even with controller/autofire input.
		if (human & (1U << 4)) ai &= ~(1U << 5);
		if (human & (1U << 5)) ai &= ~(1U << 4);
		if (human & (1U << 6)) ai &= ~(1U << 7);
		if (human & (1U << 7)) ai &= ~(1U << 6);
		return (human | ai) & 0x3FF;
	}

private:
	std::vector<Step> m_steps;
	std::size_t m_completed = 0;
	int m_remaining = 0;
	uint64_t m_start = 0;
	uint64_t m_end = 0;
};
}
