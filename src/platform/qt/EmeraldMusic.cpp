#include "EmeraldMusic.h"
#include <cmath>

#include <mgba/core/core.h>
#include <mgba/core/sync.h>
#include <mgba-util/vfs.h>
#ifdef M_CORE_GBA
#include <mgba/gba/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#endif

using namespace QGBA;

namespace {
constexpr unsigned Main = 0x030022C0;
constexpr unsigned Idle = 0x0203FFF0;
constexpr unsigned Magic = 0x68736D53;
bool ram(unsigned address, unsigned size) {
	return (address >= 0x02000000 && uint64_t(address) + size <= 0x02040000) ||
	       (address >= 0x03000000 && uint64_t(address) + size <= 0x03008000);
}
}

EmeraldMusic::EmeraldMusic() {
	mAudioBufferInit(&m_buffer, 4096, 2);
}

EmeraldMusic::~EmeraldMusic() {
	destroy();
	mAudioBufferDeinit(&m_buffer);
}

void EmeraldMusic::destroy() {
	if (m_music) {
		mCoreConfigDeinit(&m_music->config);
		m_music->deinit(m_music);
		m_music = nullptr;
	}
	m_state.clear();
}

void EmeraldMusic::reset(mCore* source) {
#ifdef M_CORE_GBA
	auto* gba = static_cast<GBA*>(source->board);
	mCoreSyncLockAudio(gba->sync);
	if (source->audioPlaybackBuffer == &m_buffer) source->audioPlaybackBuffer = nullptr;
	gba->audio.externalPlayback = false;
	mAudioBufferClear(&m_buffer);
	mCoreSyncConsumeAudio(gba->sync);
#else
	(void) source;
#endif
	m_active = false;
	m_silent = false;
	m_player = m_song = m_sourceClock = m_sourceFrame = 0;
	m_frames = 0;
	destroy();
}

unsigned EmeraldMusic::findPlayer(mCore* source) const {
	// The supported retail layouts have exactly one ten-track music player.
	// Validate its signature and track range rather than guessing an address.
	for (unsigned address = 0x03000000; address + 0x40 <= 0x03008000; address += 4) {
		if (source->rawRead32(source, address + 0x34, -1) == Magic &&
		    source->rawRead8(source, address + 8, -1) == 10 &&
		    ram(source->rawRead32(source, address + 0x2C, -1), 10 * 0x50)) return address;
	}
	return 0;
}

bool EmeraldMusic::create(mCore* source) {
#ifdef M_CORE_GBA
	auto* original = static_cast<GBA*>(source->board);
	m_music = GBACoreCreate();
	if (!m_music || !m_music->init(m_music)) {
		free(m_music);
		m_music = nullptr;
		return false;
	}
	mCoreInitConfig(m_music, nullptr);
	// Memory-backed ROM and save data; the companion never opens a save file.
	VFile* rom = VFileMemChunk(original->memory.rom, original->memory.romSize);
	if (!rom || !m_music->loadROM(m_music, rom)) {
		if (rom) rom->close(rom);
		destroy();
		return false;
	}
	auto* gba = static_cast<GBA*>(m_music->board);
	gba->romCrc32 = original->romCrc32; // GPIO modifies mapped header bytes.
	// Use the same BIOS if one was supplied, but keep it entirely memory-backed.
	if (original->biosVf) {
		VFile* bios = VFileMemChunk(original->memory.bios, 0x4000);
		if (!bios || !m_music->loadBIOS(m_music, bios, 0)) {
			if (bios) bios->close(bios);
			destroy();
			return false;
		}
	}
	m_music->reset(m_music);
	m_state.resize(int(source->stateSize(source)));
	return true;
#else
	(void) source;
	return false;
#endif
}

bool EmeraldMusic::seed(mCore* source) {
#ifdef M_CORE_GBA
	if (!m_music && !create(source)) return false;
	if (!source->saveState(source, m_state.data()) || !m_music->loadState(m_music, m_state.constData())) return false;
	auto* gba = static_cast<GBA*>(m_music->board);
	unsigned sound = m_music->rawRead32(m_music, 0x03007FF0, -1);
	if (!ram(sound, 0xFB0) || m_music->rawRead32(m_music, sound, -1) != Magic) return false;
	unsigned tracks = m_music->rawRead32(m_music, m_player + 0x2C, -1);
	if (!ram(tracks, 10 * 0x50)) return false;

	// Keep only BGM in the sound driver's player chain. SE and cry sequencers
	// never run in this instance. Existing non-BGM voices are also stopped.
	m_music->rawWrite32(m_music, sound + 0x24, -1, m_player);
	m_music->rawWrite32(m_music, m_player + 0x38, -1, 0);
	m_music->rawWrite32(m_music, m_player + 0x3C, -1, 0);
	unsigned status = m_music->rawRead32(m_music, m_player + 4, -1);
	m_music->rawWrite32(m_music, m_player + 4, -1, status & 0x7FFFFFFF);
	// Ignore temporary fanfare/cry ducking; background music stays continuous.
	m_music->rawWrite16(m_music, m_player + 0x24, -1, 0);
	for (unsigned i = 0; i < 10; ++i) {
		m_music->rawWrite8(m_music, tracks + i * 0x50 + 0x13, -1, 64); // volX
		unsigned flags = m_music->rawRead8(m_music, tracks + i * 0x50, -1);
		m_music->rawWrite8(m_music, tracks + i * 0x50, -1, flags | 3); // volume/pitch changed
	}
	auto keepVoice = [&](unsigned channel) {
		unsigned track = m_music->rawRead32(m_music, channel + 0x2C, -1);
		return track >= tracks && track < tracks + 10 * 0x50 && (track - tracks) % 0x50 == 0;
	};
	for (unsigned i = 0; i < 12; ++i) {
		unsigned channel = sound + 0x50 + i * 0x40;
		if (!keepVoice(channel)) m_music->rawWrite8(m_music, channel, -1, 0);
	}
	unsigned cgb = m_music->rawRead32(m_music, sound + 0x1C, -1);
	if (ram(cgb, 4 * 0x40)) {
		bool* playing[] = { &gba->audio.psg.playingCh1, &gba->audio.psg.playingCh2, &gba->audio.psg.playingCh3, &gba->audio.psg.playingCh4 };
		for (unsigned i = 0; i < 4; ++i) {
			if (keepVoice(cgb + i * 0x40)) continue;
			m_music->rawWrite8(m_music, cgb + i * 0x40, -1, 0);
			*playing[i] = false;
		}
	}
	// Old mixed PCM/FIFO samples may contain a menu beep or cry. Only newly
	// generated BGM should reach the device after a song change.
	for (unsigned offset = 0x350; offset < 0xFB0; offset += 4) m_music->rawWrite32(m_music, sound + offset, -1, 0);
	memset(gba->audio.chA.fifo, 0, sizeof(gba->audio.chA.fifo));
	memset(gba->audio.chB.fifo, 0, sizeof(gba->audio.chB.fifo));
	gba->audio.chA.internalSample = gba->audio.chB.internalSample = 0;
	memset(gba->audio.chA.samples, 0, sizeof(gba->audio.chA.samples));
	memset(gba->audio.chB.samples, 0, sizeof(gba->audio.chB.samples));
	memset(gba->audio.currentSamples, 0, sizeof(gba->audio.currentSamples));
	// VBlank still invokes m4aSoundMain; VCount still invokes SoundVSync.
	// Main, graphics and gameplay callbacks never execute in the companion.
	for (unsigned offset : { 0u, 4u, 0xCu, 0x10u, 0x14u, 0x18u }) m_music->rawWrite32(m_music, Main + offset, -1, 0);
	m_music->rawWrite16(m_music, Idle, -1, 0xE7FE); // Thumb B .
	m_music->writeRegister(m_music, "cpsr", MODE_SYSTEM | 0x20); // Thumb, IRQ enabled
	gba->cpu->halted = 0;
	m_music->writeRegister(m_music, "pc", Idle);
	gba->idleLoop = Idle;
	gba->idleOptimization = IDLE_LOOP_REMOVE;
	mAudioBufferClear(m_music->getAudioBuffer(m_music));
	m_frames = 1; // Start producing immediately, rather than wait eight frames.
	return true;
#else
	(void) source;
	return false;
#endif
}

unsigned EmeraldMusic::musicClock() const {
	return m_music && m_player ? m_music->rawRead32(m_music, m_player + 0xC, -1) : 0;
}

void EmeraldMusic::update(mCore* source, double speed, bool supported) {
#ifdef M_CORE_GBA
	if (!supported || source->platform(source) != mPLATFORM_GBA || !std::isfinite(speed) || speed <= 1.01) {
		if (m_active || m_music) reset(source);
		return;
	}
	if (!m_player) m_player = findPlayer(source);
	if (!m_player) return; // Sound driver has not initialized yet.
	unsigned song = source->rawRead32(source, m_player, -1);
	unsigned clock = source->rawRead32(source, m_player + 0xC, -1);
	unsigned frame = source->frameCounter(source);
	unsigned status = source->rawRead32(source, m_player + 4, -1);
	// A temporary fanfare pause retains the track mask; a completed fade-out
	// clears it. Keep BGM through fanfares, but respect genuine silence.
	bool silent = !song || ((status & 0x80000000) && !(status & 0xFFFF));
	bool restarted = clock < m_sourceClock || frame < m_sourceFrame;
	if (!m_active || song != m_song || restarted || (m_silent && !silent)) {
		if (!seed(source)) {
			reset(source);
			return;
		}
		m_song = song;
		auto* original = static_cast<GBA*>(source->board);
		mCoreSyncLockAudio(original->sync);
		mAudioBufferClear(&m_buffer);
		source->audioPlaybackBuffer = &m_buffer;
		original->audio.externalPlayback = true;
		mCoreSyncConsumeAudio(original->sync);
		m_active = true;
	}
	if (silent && !m_silent) {
		auto* original = static_cast<GBA*>(source->board);
		mCoreSyncLockAudio(original->sync);
		mAudioBufferClear(&m_buffer);
		mCoreSyncConsumeAudio(original->sync);
	}
	m_silent = silent;
	m_sourceClock = clock;
	m_sourceFrame = frame;
	m_frames += 1 / speed;
	if (m_frames < 1) return;
	m_frames -= 1;
	auto* original = static_cast<GBA*>(source->board);
	auto* gba = static_cast<GBA*>(m_music->board);
	gba->audio.masterVolume = original->audio.masterVolume;
	m_music->runFrame(m_music);
	int16_t samples[2048 * 2];
	size_t available = mAudioBufferRead(m_music->getAudioBuffer(m_music), samples, 2048);
	if (m_silent) memset(samples, 0, available * 4);
	mCoreSyncLockAudio(original->sync);
	size_t room = mAudioBufferCapacity(&m_buffer) - mAudioBufferAvailable(&m_buffer);
	if (available > room) mAudioBufferRead(&m_buffer, nullptr, available - room);
	mAudioBufferWrite(&m_buffer, samples, available);
	// Discard accelerated game audio; it must never create a delayed queue.
	mAudioBufferClear(source->getAudioBuffer(source));
	if (!mCoreSyncProduceAudio(original->sync, &m_buffer)) GBAInterrupt(original);
#else
	(void) source; (void) speed; (void) supported;
#endif
}
