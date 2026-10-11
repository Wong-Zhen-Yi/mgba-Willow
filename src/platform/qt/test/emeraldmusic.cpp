#include "../EmeraldMusic.h"
#include "../EmeraldGameState.h"

#include <QCryptographicHash>
#include <QFile>
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/core/sync.h>
#include <mgba/core/thread.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/vfs.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed line " << __LINE__ << ": " << #condition << '\n'; std::abort(); } } while (0)

static void checkInterruptedPlayback(const char* romPath) {
	mCore* core = GBACoreCreate();
	CHECK(core && core->init(core));
	mCoreInitConfig(core, nullptr);
	CHECK(core->loadROM(core, VFileOpen(romPath, O_RDONLY)));
	mAudioBuffer playback;
	mAudioBufferInit(&playback, 4096, 2);
	std::atomic<unsigned> frames{0};
	mCoreThread thread{};
	thread.core = core;
	thread.logger.logger = mLogGetContext();
	thread.userData = &frames;
	thread.frameCallback = [](mCoreThread* context) {
		++*static_cast<std::atomic<unsigned>*>(context->userData);
	};
	CHECK(mCoreThreadStart(&thread));
	mCoreThreadInterrupt(&thread);
	core->audioPlaybackBuffer = &playback;
	static_cast<GBA*>(core->board)->audio.externalPlayback = true;
	thread.impl->sync.audioWait = true;
	thread.impl->sync.audioHighWater = 512;
	thread.impl->sync.videoFrameWait = false;
	// The backend consumes playback, leaving raw game audio above the water
	// mark. Continuing an inspection must not wait on the unconsumed buffer.
	mCoreSyncLockAudio(&thread.impl->sync);
	int16_t samples[1024 * 2]{};
	mAudioBufferWrite(core->getAudioBuffer(core), samples, 1024);
	mCoreSyncUnlockAudio(&thread.impl->sync);
	unsigned start = frames.load();
	mCoreThreadContinue(&thread);
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (frames.load() < start + 2 && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	bool advanced = frames.load() >= start + 2;
	mCoreThreadEnd(&thread);
	mCoreThreadJoin(&thread);
	core->audioPlaybackBuffer = nullptr;
	mAudioBufferDeinit(&playback);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	CHECK(advanced);
	std::cout << "Interrupted external playback resumed without waiting on raw audio.\n";
}

static QByteArray render(mCore* core, double speed) {
	QGBA::EmeraldMusic music;
	QByteArray pcm;
	for (unsigned i = 0; i < unsigned(60 * speed); ++i) {
		music.update(core, speed, true);
		CHECK(music.active());
		CHECK(core->audioPlaybackBuffer);
		CHECK(mAudioBufferAvailable(core->audioPlaybackBuffer) <= 4096);
		int16_t samples[4096 * 2];
		size_t count = mAudioBufferRead(core->audioPlaybackBuffer, samples, 4096);
		pcm.append(reinterpret_cast<const char*>(samples), int(count * 4));
	}
	std::cout << speed << "x: " << pcm.size() / 4 << " native-rate music samples, music clock " << music.musicClock() << '\n';
	double duration = (pcm.size() / 4) / double(core->audioSampleRate(core));
	CHECK(duration > 0.95 && duration < 1.1);
	bool audible = false;
	for (int i = 0; i < pcm.size(); ++i) audible |= pcm[i] != 0;
	CHECK(audible);
	music.update(core, 1, true);
	CHECK(!music.active());
	CHECK(!core->audioPlaybackBuffer);
	return pcm;
}

int main(int argc, char** argv) {
	mLogger logger{};
	mLogFilter filter;
	mLogFilterInit(&filter);
	filter.defaultLevels = mLOG_WARN | mLOG_ERROR | mLOG_FATAL;
	logger.filter = &filter;
	logger.log = [](mLogger*, int, mLogLevel, const char* format, va_list args) { vfprintf(stderr, format, args); fputc('\n', stderr); };
	mLogSetDefaultLogger(&logger);
	mCore* core = GBACoreCreate();
	CHECK(core && core->init(core));
	mCoreInitConfig(core, nullptr);
	{
		QGBA::EmeraldMusic music;
		music.update(core, 8, false);
		CHECK(!music.active() && !core->audioPlaybackBuffer);
		music.update(core, 8, true); // Uninitialized driver must fall back safely.
		CHECK(!music.active() && !core->audioPlaybackBuffer);
	}
	if (argc >= 3) {
		checkInterruptedPlayback(argv[1]);
		VFile* rom = VFileOpen(argv[1], O_RDONLY);
		CHECK(rom && core->loadROM(core, rom));
		core->reset(core);
		VFile* state = VFileOpen(argv[2], O_RDONLY);
		CHECK(state && mCoreLoadStateNamed(core, state, 0));
		state->close(state);
		QGBA::EmeraldGameState identity;
		identity.identify(core);
		CHECK(identity.supported());
		QByteArray before(int(core->stateSize(core)), '\0');
		CHECK(core->saveState(core, before.data()));
		QByteArray eight = render(core, 8);
		QByteArray sixteen = render(core, 16);
		CHECK(eight == sixteen); // Same real duration, identical music at both game speeds.
		QByteArray after(before.size(), '\0');
		CHECK(core->saveState(core, after.data()));
		CHECK(before == after); // Music playback must not advance or patch gameplay.
		if (argc >= 5) {
			QGBA::EmeraldMusic music;
			music.update(core, 8, true);
			unsigned previousSong = music.song();
			VFile* next = VFileOpen(argv[4], O_RDONLY);
			CHECK(next && mCoreLoadStateNamed(core, next, 0));
			next->close(next);
			music.update(core, 8, true);
			CHECK(music.active() && music.song() != previousSong);
			std::cout << "Song transition followed: " << std::hex << previousSong << " -> " << music.song() << std::dec << '\n';
			music.update(core, 8, false);
			CHECK(!music.active() && !core->audioPlaybackBuffer);
			CHECK(core->loadState(core, before.constData()));
		}
		// Exercise the main audio producer with audio sync enabled. Accelerated
		// raw PCM must not block the frame that produces companion music.
		mCoreSync sync{};
		MutexInit(&sync.audioBufferMutex);
		MutexInit(&sync.videoFrameMutex);
		ConditionInit(&sync.audioRequiredCond);
		ConditionInit(&sync.videoFrameAvailableCond);
		ConditionInit(&sync.videoFrameRequiredCond);
		sync.audioWait = true;
		sync.audioHighWater = 4096;
		core->setSync(core, &sync);
		{
			QGBA::EmeraldMusic music;
			music.update(core, 8, true);
			mAudioBufferClear(core->audioPlaybackBuffer);
			unsigned start = core->frameCounter(core);
			for (unsigned i = 0; i < 240; ++i) {
				core->runFrame(core);
				music.update(core, 8, true);
				CHECK(music.active());
				mAudioBufferClear(core->audioPlaybackBuffer);
			}
			CHECK(core->frameCounter(core) == start + 240);
			music.reset(core);
		}
		core->setSync(core, nullptr);
		ConditionDeinit(&sync.audioRequiredCond);
		ConditionDeinit(&sync.videoFrameAvailableCond);
		ConditionDeinit(&sync.videoFrameRequiredCond);
		MutexDeinit(&sync.audioBufferMutex);
		MutexDeinit(&sync.videoFrameMutex);
		std::cout << "Audio sync checks passed; 240 accelerated gameplay frames completed.\n";
		if (argc >= 4) {
			QFile pcm(QString::fromLocal8Bit(argv[3]));
			CHECK(pcm.open(QIODevice::WriteOnly) && pcm.write(eight) == eight.size());
		}
	} else {
		std::cout << "Fallback checks passed. Pass ROM and checkpoint for real music integration checks.\n";
	}
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	mLogSetDefaultLogger(nullptr);
	mLogFilterDeinit(&filter);
	return 0;
}
