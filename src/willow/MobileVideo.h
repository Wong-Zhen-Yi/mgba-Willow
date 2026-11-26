/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace Willow {

// One frame of 8-bit RGBX pixels: bytes R, G, B, then one unused byte.
struct VideoFrame {
	const uint8_t* pixels = nullptr;
	int width = 0;
	int height = 0;
	int stride = 0; // Bytes per row; at least width * 4.
};

struct VideoSize {
	int width = 0;
	int height = 0;
	bool operator==(const VideoSize& other) const { return width == other.width && height == other.height; }
	bool operator!=(const VideoSize& other) const { return !(*this == other); }
};

// Low-latency HEVC via NVIDIA NVENC, or H.264 with hardware encoders
// preferred over x264. Native-size frames are
// scaled by an integer factor with nearest-neighbour sampling so 4:2:0 chroma
// never spans two console pixels; frames 480 or more pixels wide pass unscaled.
// Output is Annex B with in-band parameter sets and no B-frames, which
// WebCodecs can decode without extra configuration.
class MobileVideoEncoder {
public:
	struct Packet {
		std::vector<uint8_t> data;
		bool key = false;
	};

	static bool available();
	static std::vector<std::string> defaultEncoders(bool hevc = false);

	explicit MobileVideoEncoder(std::vector<std::string> encoders = defaultEncoders(), int scale = 2);
	~MobileVideoEncoder();
	MobileVideoEncoder(const MobileVideoEncoder&) = delete;
	MobileVideoEncoder& operator=(const MobileVideoEncoder&) = delete;

	// Encodes one frame and appends any finished access units. Returns false
	// once every candidate encoder has failed; error() then explains why.
	bool encode(const VideoFrame& frame, bool keyframe, std::vector<Packet>* packets);

	std::string encoder() const { return m_name; }
	bool hardware() const;
	// RFC 6381 codec string, such as "avc1.4d401e", known after the first keyframe.
	std::string codec() const { return m_codec; }
	VideoSize size() const { return m_size; }
	std::string error() const { return m_error; }

private:
	bool open(const VideoSize& source);
	void close();
	bool drain(std::vector<Packet>* packets);

	std::vector<std::string> m_candidates;
	int m_scale;
	std::string m_name;
	std::string m_codec;
	std::string m_error;
	VideoSize m_source;
	VideoSize m_size;
	int64_t m_pts = 0;
	bool m_needKey = true;
	AVCodecContext* m_context = nullptr;
	AVFrame* m_frame = nullptr;
	AVPacket* m_packet = nullptr;
	SwsContext* m_scaler = nullptr;
};

}
