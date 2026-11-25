/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MobileVideo.h"

#include <mgba/flags.h>

#include <QColor>
#include <QElapsedTimer>
#include <QPainter>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#ifdef USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#endif

using namespace QGBA;

#define CHECK(condition)                                                      \
	do {                                                                      \
		if (!(condition)) {                                                   \
			std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #condition); \
			std::exit(1);                                                     \
		}                                                                     \
	} while (0)

namespace {

// Pixel-art-like frame: solid 8x8 tiles of saturated colours that scroll.
QImage pattern(int width, int height, int shift) {
	static const QRgb palette[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff,
	                               0xffffff, 0x000000, 0x806040, 0x208040, 0xf0a0c0, 0x404080};
	QImage image(width, height, QImage::Format_RGBX8888);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int tile = ((x + shift) / 8) * 7 + (y / 8) * 3;
			image.setPixel(x, y, palette[tile % 12]);
		}
	}
	return image;
}

#ifdef USE_FFMPEG
struct Decoder {
	AVCodecContext* context = nullptr;
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	QList<QImage> frames;

	Decoder() {
		const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		CHECK(codec);
		context = avcodec_alloc_context3(codec);
		context->flags |= AV_CODEC_FLAG_LOW_DELAY;
		CHECK(avcodec_open2(context, codec, nullptr) >= 0);
	}
	~Decoder() {
		avcodec_free_context(&context);
		av_packet_free(&packet);
		av_frame_free(&frame);
	}
	void decode(const QByteArray& data) {
		packet->data = reinterpret_cast<uint8_t*>(const_cast<char*>(data.constData()));
		packet->size = data.size();
		CHECK(avcodec_send_packet(context, packet) >= 0);
		receive();
	}
	void flush() {
		avcodec_send_packet(context, nullptr);
		receive();
	}
	void receive() {
		while (avcodec_receive_frame(context, frame) >= 0) {
			QImage image(frame->width, frame->height, QImage::Format_RGBX8888);
			SwsContext* scaler = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format),
			                                    frame->width, frame->height, AV_PIX_FMT_RGB0, SWS_POINT, nullptr,
			                                    nullptr, nullptr);
			const int* matrix = sws_getCoefficients(SWS_CS_ITU601);
			sws_setColorspaceDetails(scaler, matrix, 0, matrix, 1, 0, 1 << 16, 1 << 16);
			uint8_t* planes[] = {image.bits()};
			const int strides[] = {int(image.bytesPerLine())};
			sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes, strides);
			sws_freeContext(scaler);
			frames.append(image);
		}
	}
};

int channelError(QRgb a, QRgb b) {
	return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
}

// Encodes a scrolling clip, decodes it, and checks keyframes and colour fidelity.
bool exercise(const QString& name) {
	MobileVideoEncoder encoder({name});
	Decoder decoder;
	QList<QImage> sources;
	int keys = 0;
	QElapsedTimer timer;
	timer.start();
	for (int i = 0; i < 90; ++i) {
		QList<MobileVideoEncoder::Packet> packets;
		const QImage source = pattern(240, 160, i);
		if (!encoder.encode(source, i == 60, &packets)) {
			std::printf("skip %s: %s\n", qPrintable(name), qPrintable(encoder.error()));
			return false;
		}
		sources.append(source);
		for (const auto& packet : packets) {
			if (packet.key) ++keys;
			decoder.decode(packet.data);
		}
	}
	const double milliseconds = timer.nsecsElapsed() / 1e6 / 90;
	decoder.flush();
	CHECK(encoder.encoder() == name);
	CHECK(encoder.size() == QSize(480, 320));
	CHECK(encoder.codec().startsWith("avc1."));
	CHECK(encoder.codec().size() == 11);
	// The first frame and the forced one at 60, and nothing else.
	CHECK(keys == 2);
	CHECK(decoder.frames.size() == sources.size());

	int worst = 0;
	double total = 0;
	int samples = 0;
	for (int i = 30; i < sources.size(); ++i) {
		const QImage& decoded = decoder.frames[i];
		CHECK(decoded.size() == QSize(480, 320));
		// Sample tile centres, away from DCT ringing at the edges.
		for (int y = 4; y < 160; y += 8) {
			for (int x = 4; x < 240; x += 8) {
				const int tileX = ((x + i) / 8) * 8 - i;
				if (tileX < 0 || tileX + 8 > 240) continue;
				const int cx = tileX + 4;
				const int error = channelError(sources[i].pixel(cx, y), decoded.pixel(cx * 2, y * 2));
				worst = std::max(worst, error);
				total += error;
				++samples;
			}
		}
	}
	const double mean = total / samples;
	std::printf("%s: codec %s, %.2f ms/frame, mean error %.2f, worst %d\n", qPrintable(name),
	            qPrintable(encoder.codec()), milliseconds, mean, worst);
	// A BT.601/BT.709 mismatch shifts saturated colours by 20+ levels.
	CHECK(mean < 6);
	CHECK(worst < 20);
	return true;
}
#endif

}

int main() {
#ifdef USE_FFMPEG
	CHECK(MobileVideoEncoder::available());
	CHECK(MobileVideoEncoder::defaultEncoders().first() == "h264_nvenc");
	CHECK(MobileVideoEncoder::defaultEncoders().last() == "libx264");

	// Software encoding must always work; hardware runs where the machine has it.
	CHECK(exercise("libx264"));
	for (const char* name : {"h264_nvenc", "h264_amf", "h264_qsv", "h264_mf"}) exercise(name);

	{
		// Unusable candidates are skipped in order.
		MobileVideoEncoder encoder({"not_an_encoder", "libx264"});
		QList<MobileVideoEncoder::Packet> packets;
		CHECK(encoder.encode(pattern(240, 160, 0), false, &packets));
		CHECK(encoder.encoder() == "libx264");
		CHECK(!encoder.hardware());
		CHECK(!packets.isEmpty() && packets.first().key);

		// A size change (GBA to Game Boy) reopens the encoder with a keyframe.
		packets.clear();
		CHECK(encoder.encode(pattern(240, 160, 1), false, &packets));
		CHECK(packets.size() == 1 && !packets.first().key);
		packets.clear();
		CHECK(encoder.encode(pattern(160, 144, 0), false, &packets));
		CHECK(encoder.size() == QSize(320, 288));
		CHECK(!packets.isEmpty() && packets.first().key);
	}
	{
		MobileVideoEncoder encoder({"not_an_encoder"});
		QList<MobileVideoEncoder::Packet> packets;
		CHECK(!encoder.encode(pattern(240, 160, 0), false, &packets));
		CHECK(encoder.error().contains("not_an_encoder"));
		CHECK(packets.isEmpty());
	}
	{
		MobileVideoEncoder encoder({"libx264"}, 1);
		QList<MobileVideoEncoder::Packet> packets;
		CHECK(encoder.encode(pattern(240, 160, 0).convertToFormat(QImage::Format_RGB32), false, &packets));
		CHECK(encoder.size() == QSize(240, 160));
	}
	{
		// Frames the OpenGL renderer already upscaled are encoded as they are.
		MobileVideoEncoder encoder({"libx264"});
		QList<MobileVideoEncoder::Packet> packets;
		CHECK(encoder.encode(pattern(960, 640, 0), false, &packets));
		CHECK(encoder.size() == QSize(960, 640));
	}
#else
	CHECK(!MobileVideoEncoder::available());
#endif
	std::puts("mobilevideo: ok");
	return 0;
}
