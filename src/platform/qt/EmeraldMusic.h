#pragma once

#include <QByteArray>
#include <mgba-util/audio-buffer.h>

struct mCore;

namespace QGBA {

// Runs only Emerald's sound driver in a private, disk-free GBA instance.
// All methods run on the main emulation thread (or while it is interrupted).
class EmeraldMusic {
public:
	EmeraldMusic();
	~EmeraldMusic();
	EmeraldMusic(const EmeraldMusic&) = delete;
	EmeraldMusic& operator=(const EmeraldMusic&) = delete;
	void update(mCore* source, double speed, bool supported);
	void reset(mCore* source);
	bool active() const { return m_active; }
	unsigned song() const { return m_song; }
	unsigned musicClock() const;

private:
	bool create(mCore* source);
	bool seed(mCore* source);
	void destroy();
	unsigned findPlayer(mCore* source) const;
	mCore* m_music = nullptr;
	QByteArray m_state;
	mAudioBuffer m_buffer;
	unsigned m_player = 0;
	unsigned m_song = 0;
	unsigned m_sourceClock = 0;
	unsigned m_sourceFrame = 0;
	double m_frames = 0;
	bool m_active = false;
	bool m_silent = false;
};

}
