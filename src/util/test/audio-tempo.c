/* Pitch and duration regression checks for streaming fast-forward audio. */
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #condition); abort(); } } while (0)

static void checkTempo(double speed) {
	struct mAudioBuffer source, output;
	struct mAudioResampler resampler;
	mAudioBufferInit(&source, 4096, 2);
	mAudioBufferInit(&output, 512, 2);
	mAudioResamplerInit(&resampler, mINTERPOLATOR_SINC);
	mAudioResamplerSetSource(&resampler, &source, 32768, true);
	mAudioResamplerSetDestination(&resampler, &output, 48000);
	mAudioResamplerSetTempo(&resampler, speed);
	int16_t samples[512 * 2];
	size_t count = 0, crossings = 0;
	int16_t previous = 0;
	for (size_t offset = 0; offset < 32768 * 5; offset += 256) {
		for (size_t i = 0; i < 256; ++i) {
			samples[2 * i] = 12000 * sin(2 * M_PI * 440 * (offset + i) / 32768);
			samples[2 * i + 1] = samples[2 * i] / 2;
		}
		CHECK(mAudioBufferWrite(&source, samples, 256) == 256);
		do {
			mAudioResamplerProcess(&resampler);
			size_t available = mAudioBufferRead(&output, samples, 512);
			if (!available) {
				break;
			}
			for (size_t i = 0; i < available; ++i) {
				CHECK(abs(samples[2 * i] - 2 * samples[2 * i + 1]) <= 4);
				if (previous <= 0 && samples[2 * i] > 0) {
					++crossings;
				}
				previous = samples[2 * i];
			}
			count += available;
		} while (true);
	}
	double duration = count / 48000.0;
	double pitch = crossings / duration;
	printf("%.1fx: %.3f seconds, %.1f Hz\n", speed, duration, pitch);
	fflush(stdout);
	CHECK(fabs(duration - 5 / speed) < 0.05 + 0.02 * 5 / speed);
	CHECK(fabs(pitch - 440) < 10);
	// Switching back releases the tempo queue and resumes ordinary resampling.
	mAudioResamplerSetTempo(&resampler, 1);
	mAudioResamplerProcess(&resampler);
	CHECK(!resampler.tempoState);
	mAudioResamplerDeinit(&resampler);
	mAudioBufferDeinit(&source);
	mAudioBufferDeinit(&output);
}

int main(void) {
	checkTempo(1);
	checkTempo(1.5);
	checkTempo(2);
	checkTempo(8);
	checkTempo(16);
	return 0;
}
