/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../MobileVideo.h"

#include <mgba/flags.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#endif

using namespace Willow;

#define CHECK(condition)                                                      \
	do {                                                                      \
		if (!(condition)) {                                                   \
			std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #condition); \
			std::exit(1);                                                     \
		}                                                                     \
	} while (0)

namespace {

// An owned RGBX image, as VideoFrame describes.
struct Image {
	int width = 0;
	int height = 0;
	int stride = 0;
	std::vector<uint8_t> bytes;

	Image() = default;
	Image(int width, int height, int padding = 0)
		: width(width), height(height), stride(width * 4 + padding), bytes(size_t(stride) * height) {}
	VideoSize size() const { return { width, height }; }
	VideoFrame frame() const { return { bytes.data(), width, height, stride }; }
	uint32_t pixel(int x, int y) const {
		const uint8_t* p = &bytes[size_t(y) * stride + x * 4];
		return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
	}
	void setPixel(int x, int y, uint32_t rgb) {
		uint8_t* p = &bytes[size_t(y) * stride + x * 4];
		p[0] = rgb >> 16;
		p[1] = rgb >> 8;
		p[2] = rgb;
		p[3] = 0xFF;
	}
};

// Pixel-art-like frame: solid 8x8 tiles of saturated colours that scroll.
Image pattern(int width, int height, int shift, int padding = 0) {
	static const uint32_t palette[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff,
	                                   0xffffff, 0x000000, 0x806040, 0x208040, 0xf0a0c0, 0x404080};
	Image image(width, height, padding);
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
	std::vector<Image> frames;

	explicit Decoder(AVCodecID id) {
		const AVCodec* codec = avcodec_find_decoder(id);
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
	void decode(const std::vector<uint8_t>& data) {
		packet->data = const_cast<uint8_t*>(data.data());
		packet->size = int(data.size());
		CHECK(avcodec_send_packet(context, packet) >= 0);
		receive();
	}
	void flush() {
		avcodec_send_packet(context, nullptr);
		receive();
	}
	void receive() {
		while (avcodec_receive_frame(context, frame) >= 0) {
			Image image(frame->width, frame->height);
			SwsContext* scaler = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format),
			                                    frame->width, frame->height, AV_PIX_FMT_RGB0, SWS_POINT, nullptr,
			                                    nullptr, nullptr);
			const int* matrix = sws_getCoefficients(SWS_CS_ITU601);
			sws_setColorspaceDetails(scaler, matrix, 0, matrix, 1, 0, 1 << 16, 1 << 16);
			uint8_t* planes[] = {image.bytes.data()};
			const int strides[] = {image.stride};
			sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes, strides);
			sws_freeContext(scaler);
			frames.push_back(std::move(image));
		}
	}
};

int channelError(uint32_t a, uint32_t b) {
	int worst = 0;
	for (int shift : {16, 8, 0})
		worst = std::max(worst, std::abs(int((a >> shift) & 0xFF) - int((b >> shift) & 0xFF)));
	return worst;
}

bool startsWith(const std::string& text, const char* prefix) { return text.rfind(prefix, 0) == 0; }

// Encodes a scrolling clip, decodes it, and checks keyframes and colour fidelity.
bool exercise(const std::string& name, AVCodecID id = AV_CODEC_ID_H264) {
	MobileVideoEncoder encoder({name});
	Decoder decoder(id);
	Decoder lateDecoder(id);
	std::vector<Image> sources;
	int keys = 0;
	const auto started = std::chrono::steady_clock::now();
	for (int i = 0; i < 90; ++i) {
		std::vector<MobileVideoEncoder::Packet> packets;
		Image source = pattern(240, 160, i);
		if (!encoder.encode(source.frame(), i == 60, &packets)) {
			std::printf("skip %s: %s\n", name.c_str(), encoder.error().c_str());
			return false;
		}
		sources.push_back(std::move(source));
		for (const auto& packet : packets) {
			if (packet.key) ++keys;
			decoder.decode(packet.data);
			if (keys == 2)
				lateDecoder.decode(packet.data);
		}
	}
	const double milliseconds =
	    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() / 90;
	decoder.flush();
	lateDecoder.flush();
	CHECK(encoder.encoder() == name);
	CHECK(encoder.size() == VideoSize({480, 320}));
	if (id == AV_CODEC_ID_HEVC) {
		CHECK(startsWith(encoder.codec(), "hvc1.1."));
		CHECK(encoder.hardware());
	} else {
		CHECK(startsWith(encoder.codec(), "avc1."));
		CHECK(encoder.codec().size() == 11);
	}
	// The first frame and the forced one at 60, and nothing else.
	CHECK(keys == 2);
	CHECK(decoder.frames.size() == sources.size());
	CHECK(lateDecoder.frames.size() == sources.size() - 60);

	int worst = 0;
	double total = 0;
	int samples = 0;
	for (size_t i = 0; i < sources.size(); ++i) {
		if (i < 30) continue;
		const Image& decoded = decoder.frames[i];
		CHECK(decoded.size() == VideoSize({480, 320}));
		// Sample tile centres, away from DCT ringing at the edges.
		for (int y = 4; y < 160; y += 8) {
			for (int x = 4; x < 240; x += 8) {
				const int tileX = ((x + int(i)) / 8) * 8 - int(i);
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
	std::printf("%s: codec %s, %.2f ms/frame, mean error %.2f, worst %d\n", name.c_str(), encoder.codec().c_str(),
	            milliseconds, mean, worst);
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
	CHECK(MobileVideoEncoder::defaultEncoders().front() == "h264_nvenc");
	CHECK(MobileVideoEncoder::defaultEncoders().back() == "libx264");
	CHECK(MobileVideoEncoder::defaultEncoders(true) == std::vector<std::string> { "hevc_nvenc" });

	// Software encoding must always work; hardware runs where the machine has it.
	CHECK(exercise("libx264"));
	exercise("hevc_nvenc", AV_CODEC_ID_HEVC);
	for (const char* name : {"h264_nvenc", "h264_amf", "h264_qsv", "h264_mf"}) exercise(name);

	{
		// Unusable candidates are skipped in order.
		MobileVideoEncoder encoder({"not_an_encoder", "libx264"});
		std::vector<MobileVideoEncoder::Packet> packets;
		CHECK(encoder.encode(pattern(240, 160, 0).frame(), false, &packets));
		CHECK(encoder.encoder() == "libx264");
		CHECK(!encoder.hardware());
		CHECK(!packets.empty() && packets.front().key);

		// A size change (GBA to Game Boy) reopens the encoder with a keyframe.
		packets.clear();
		CHECK(encoder.encode(pattern(240, 160, 1).frame(), false, &packets));
		CHECK(packets.size() == 1 && !packets.front().key);
		packets.clear();
		CHECK(encoder.encode(pattern(160, 144, 0).frame(), false, &packets));
		CHECK(encoder.size() == VideoSize({320, 288}));
		CHECK(!packets.empty() && packets.front().key);
	}
	{
		MobileVideoEncoder encoder({"not_an_encoder"});
		std::vector<MobileVideoEncoder::Packet> packets;
		CHECK(!encoder.encode(pattern(240, 160, 0).frame(), false, &packets));
		CHECK(encoder.error().find("not_an_encoder") != std::string::npos);
		CHECK(packets.empty());
	}
	{
		MobileVideoEncoder encoder({"libx264"}, 1);
		std::vector<MobileVideoEncoder::Packet> packets;
		// Rows may be padded past width * 4 bytes.
		CHECK(encoder.encode(pattern(240, 160, 0, 64).frame(), false, &packets));
		CHECK(encoder.size() == VideoSize({240, 160}));
	}
	{
		// Frames the OpenGL renderer already upscaled are encoded as they are.
		MobileVideoEncoder encoder({"libx264"});
		std::vector<MobileVideoEncoder::Packet> packets;
		CHECK(encoder.encode(pattern(960, 640, 0).frame(), false, &packets));
		CHECK(encoder.size() == VideoSize({960, 640}));
	}
#else
	CHECK(!MobileVideoEncoder::available());
#endif
	std::puts("mobilevideo: ok");
	return 0;
}
