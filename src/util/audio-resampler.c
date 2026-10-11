/* Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba-util/audio-resampler.h>

#include <mgba-util/audio-buffer.h>
#include "third-party/sonic/sonic.h"

#define MAX_CHANNELS 2

struct mAudioTempoState {
	sonicStream stream;
	struct mAudioBuffer input;
	double rate;
	double tempo;
	unsigned channels;
};

static void _destroyTempo(struct mAudioResampler* resampler) {
	if (!resampler->tempoState) {
		return;
	}
	sonicDestroyStream(resampler->tempoState->stream);
	mAudioBufferDeinit(&resampler->tempoState->input);
	free(resampler->tempoState);
	resampler->tempoState = NULL;
}

void mAudioResamplerSetTempo(struct mAudioResampler* resampler, double tempo) {
	resampler->tempo = isfinite(tempo) && tempo > 1.01 ? tempo : 1;
}

struct mAudioResamplerData {
	struct mAudioResampler* resampler;
	unsigned channel;
};

static int16_t _sampleAt(int index, const void* context) {
	const struct mAudioResamplerData* data = context;
	if (index < 0) {
		return 0;
	}
	return mAudioBufferPeek(data->resampler->source, data->channel, index);
}

void mAudioResamplerInit(struct mAudioResampler* resampler, enum mInterpolatorType interpType) {
	memset(resampler, 0, sizeof(*resampler));
	resampler->interpType = interpType;
	switch (interpType) {
	case mINTERPOLATOR_SINC:
		mInterpolatorSincInit(&resampler->sinc, 0, 0);
		resampler->lowWaterMark = resampler->sinc.width;
		resampler->highWaterMark = resampler->sinc.width;
		break;
	case mINTERPOLATOR_COSINE:
		mInterpolatorCosineInit(&resampler->cosine, 0);
		resampler->lowWaterMark = 0;
		resampler->highWaterMark = 1;
		break;
	}
}

void mAudioResamplerDeinit(struct mAudioResampler* resampler) {
	_destroyTempo(resampler);
	switch (resampler->interpType) {
	case mINTERPOLATOR_SINC:
		mInterpolatorSincDeinit(&resampler->sinc);
		break;
	case mINTERPOLATOR_COSINE:
		mInterpolatorCosineDeinit(&resampler->cosine);
		break;
	}
	resampler->source = NULL;
	resampler->destination = NULL;
}

void mAudioResamplerSetSource(struct mAudioResampler* resampler, struct mAudioBuffer* source, double rate, bool consume) {
	if (resampler->source && resampler->source != source) {
		_destroyTempo(resampler);
		resampler->timestamp = 0;
		if (resampler->destination) mAudioBufferClear(resampler->destination);
	}
	resampler->source = source;
	resampler->sourceRate = rate;
	resampler->consume = consume;
}

void mAudioResamplerSetDestination(struct mAudioResampler* resampler, struct mAudioBuffer* destination, double rate) {
	resampler->destination = destination;
	resampler->destRate = rate;
}

static size_t _resample(struct mAudioResampler* resampler) {
	int16_t sampleBuffer[MAX_CHANNELS] = {0};
	double timestep = resampler->sourceRate / resampler->destRate;
	double timestamp = resampler->timestamp;
	struct mInterpolator* interp = &resampler->interp;
	struct mAudioResamplerData context = {
		.resampler = resampler,
	};
	struct mInterpolationData data = {
		.at = _sampleAt,
		.context = &context,
	};

	size_t read = 0;
	mASSERT(resampler->source->channels <= MAX_CHANNELS);

	while (true) {
		if (timestamp + resampler->highWaterMark >= mAudioBufferAvailable(resampler->source)) {
			break;
		}
		if (mAudioBufferFull(resampler->destination)) {
			break;
		}

		size_t channel;
		for (channel = 0; channel < resampler->source->channels; ++channel) {
			context.channel = channel;
			sampleBuffer[channel] = interp->interpolate(interp, &data, timestamp, timestep);
		}
		if (!mAudioBufferWrite(resampler->destination, sampleBuffer, 1)) {
			break;
		}
		timestamp += timestep;
		++read;
	}

	if (resampler->consume && timestamp > resampler->lowWaterMark) {
		size_t drop = timestamp - resampler->lowWaterMark;
		drop = mAudioBufferRead(resampler->source, NULL, drop);
		timestamp -= drop;
	}
	resampler->timestamp = timestamp;
	return read;
}

size_t mAudioResamplerProcess(struct mAudioResampler* resampler) {
	if (resampler->tempo <= 1.01) {
		if (resampler->tempoState) {
			_destroyTempo(resampler);
			mAudioBufferClear(resampler->destination);
			resampler->timestamp = 0;
		}
		return _resample(resampler);
	}

	struct mAudioTempoState* state = resampler->tempoState;
	if (state && (state->rate != resampler->destRate || state->channels != resampler->destination->channels || state->tempo != resampler->tempo)) {
		_destroyTempo(resampler);
		state = NULL;
	}
	if (!state) {
		state = calloc(1, sizeof(*state));
		if (!state) {
			return 0;
		}
		state->stream = sonicCreateStream(resampler->destRate, resampler->destination->channels);
		if (!state->stream) {
			free(state);
			return 0;
		}
		state->rate = resampler->destRate;
		state->channels = resampler->destination->channels;
		state->tempo = resampler->tempo;
		sonicSetSpeed(state->stream, state->tempo);
		mAudioBufferInit(&state->input, 512, state->channels);
		resampler->tempoState = state;
		// Discard audio from the previous speed instead of playing a stale queue.
		mAudioBufferClear(resampler->destination);
		resampler->timestamp = 0;
	}

	struct mAudioBuffer* destination = resampler->destination;
	int16_t samples[512 * MAX_CHANNELS];
	size_t written = 0;
	while (!mAudioBufferFull(destination)) {
		size_t room = mAudioBufferCapacity(destination) - mAudioBufferAvailable(destination);
		int count = sonicReadShortFromStream(state->stream, samples, room < 512 ? room : 512);
		if (count) {
			written += mAudioBufferWrite(destination, samples, count);
			continue;
		}
		// Resample at the device's native rate before changing tempo. Drain in
		// bounded chunks so neither intermediate nor output queues can grow.
		resampler->destination = &state->input;
		_resample(resampler);
		resampler->destination = destination;
		count = mAudioBufferRead(&state->input, samples, 512);
		if (!count) {
			break;
		}
		if (!sonicWriteShortToStream(state->stream, samples, count)) {
			_destroyTempo(resampler);
			break;
		}
	}
	return written;
}
