/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MobileVideo.h"

#include <mgba/flags.h>

#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#ifdef USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

using namespace Willow;

namespace {

#ifdef USE_FFMPEG
std::string format(const char* pattern, ...) {
	char buffer[256];
	va_list args;
	va_start(args, pattern);
	std::vsnprintf(buffer, sizeof(buffer), pattern, args);
	va_end(args);
	return buffer;
}

bool endsWith(const std::string& text, const char* suffix) {
	const size_t length = std::char_traits<char>::length(suffix);
	return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

std::string trimmed(const std::string& text) {
	const char* space = " \t\n\v\f\r";
	const size_t start = text.find_first_not_of(space);
	if (start == std::string::npos) return {};
	return text.substr(start, text.find_last_not_of(space) - start + 1);
}

int findNal(const std::vector<uint8_t>& data, int type, bool hevc) {
	const uint8_t* bytes = data.data();
	for (int i = 0; i + 3 < int(data.size()); ++i) {
		if (bytes[i] || bytes[i + 1] || bytes[i + 2] != 1) continue;
		const int nalType = hevc ? (bytes[i + 3] >> 1) & 0x3F : bytes[i + 3] & 0x1F;
		if (nalType == type)
			return i + 3;
		i += 2;
	}
	return -1;
}

std::string hevcCodec(const std::vector<uint8_t>& data, int sps) {
	// The SPS starts with the NAL header and one byte before profile_tier_level.
	// Strip emulation-prevention bytes from its 12-byte general profile.
	std::vector<uint8_t> profile;
	int zeros = 0;
	for (int i = sps + 3; i < int(data.size()) && profile.size() < 12; ++i) {
		const uint8_t byte = data[i];
		if (zeros >= 2 && byte == 3) {
			zeros = 0;
			continue;
		}
		profile.push_back(byte);
		zeros = byte == 0 ? zeros + 1 : 0;
	}
	if (profile.size() != 12)
		return {};

	const uint8_t* bytes = profile.data();
	const int space = bytes[0] >> 6;
	const std::string prefix = space ? std::string(1, char('A' + space - 1)) : std::string();
	const char tier = bytes[0] & 0x20 ? 'H' : 'L';
	// RFC 6381 writes the 32 profile compatibility flags with the first flag in the low bit.
	uint32_t compatible = 0;
	for (int bit = 0; bit < 32; ++bit) {
		if (bytes[1 + bit / 8] & (0x80 >> (bit % 8)))
			compatible |= uint32_t(1) << bit;
	}
	std::string codec = format("hvc1.%s%d.%X.%c%d", prefix.c_str(), bytes[0] & 0x1F, unsigned(compatible), tier, bytes[11]);
	int lastConstraint = 10;
	while (lastConstraint >= 5 && !bytes[lastConstraint])
		--lastConstraint;
	for (int i = 5; i <= lastConstraint; ++i)
		codec += format(".%02X", bytes[i]);
	return codec;
}

void setOption(AVCodecContext* context, const char* key, const char* value) {
	// Each encoder names its options differently; unknown ones are ignored.
	av_opt_set(context->priv_data, key, value, 0);
}

void tune(AVCodecContext* context, const std::string& name) {
	if (endsWith(name, "_nvenc")) {
		setOption(context, "preset", "p2");
		setOption(context, "tune", "ull");
		setOption(context, "rc", "vbr");
		setOption(context, "zerolatency", "1");
		setOption(context, "delay", "0");
		setOption(context, "rc-lookahead", "0");
		setOption(context, "forced-idr", "1");
		if (name == "hevc_nvenc")
			setOption(context, "profile", "main");
	} else if (name == "h264_amf") {
		setOption(context, "usage", "ultralowlatency");
		setOption(context, "quality", "speed");
		setOption(context, "forced_idr", "1");
	} else if (name == "h264_qsv") {
		setOption(context, "preset", "veryfast");
		setOption(context, "async_depth", "1");
		setOption(context, "forced_idr", "1");
	} else if (name == "h264_mf") {
		setOption(context, "hw_encoding", "1");
		setOption(context, "scenario", "display_remoting");
		setOption(context, "rate_control", "ld_vbr");
	} else if (name == "libx264") {
		setOption(context, "preset", "superfast");
		setOption(context, "tune", "zerolatency");
		setOption(context, "forced-idr", "1");
	}
}

AVPixelFormat pixelFormat(const AVCodec* codec, const std::string& name) {
	const AVPixelFormat* formats = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
	if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
	                                 reinterpret_cast<const void**>(&formats), nullptr) < 0) {
		formats = nullptr;
	}
#else
	formats = codec->pix_fmts;
#endif
	// NVENC converts RGB to BT.601 YUV on the GPU. Other encoders get YUV from
	// swscale so the colour matrix always matches what the stream signals.
	std::vector<AVPixelFormat> preferred{AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P};
	if (name == "h264_nvenc") preferred.insert(preferred.begin(), AV_PIX_FMT_RGB0);
	if (!formats) return AV_PIX_FMT_YUV420P;
	for (AVPixelFormat format : preferred) {
		for (const AVPixelFormat* supported = formats; *supported != AV_PIX_FMT_NONE; ++supported) {
			if (*supported == format) return format;
		}
	}
	return AV_PIX_FMT_NONE;
}

std::string describe(int error) {
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(error, buffer, sizeof(buffer));
	return buffer;
}
#endif

}

bool MobileVideoEncoder::available() {
#ifdef USE_FFMPEG
	return true;
#else
	return false;
#endif
}

std::vector<std::string> MobileVideoEncoder::defaultEncoders(bool hevc) {
	// WILLOW_MOBILE_ENCODER=libx264 (or a comma-separated list) overrides the order.
	const char* variable = std::getenv("WILLOW_MOBILE_ENCODER");
	const std::string forced = trimmed(variable ? variable : "");
	if (!forced.empty()) {
		std::vector<std::string> encoders;
		size_t start = 0;
		while (start <= forced.size()) {
			size_t end = forced.find(',', start);
			if (end == std::string::npos) end = forced.size();
			const std::string name = trimmed(forced.substr(start, end - start));
			const bool isHevc = name.compare(0, 5, "hevc_") == 0;
			if (!name.empty() && isHevc == hevc)
				encoders.push_back(name);
			start = end + 1;
		}
		return encoders;
	}
	if (hevc)
		return { "hevc_nvenc" };
	return {"h264_nvenc", "h264_amf", "h264_qsv", "h264_mf", "libx264"};
}

MobileVideoEncoder::MobileVideoEncoder(std::vector<std::string> encoders, int scale)
	: m_candidates(std::move(encoders))
	, m_scale(std::clamp(scale, 1, 4)) {
	for (std::string& name : m_candidates) name = trimmed(name);
}

MobileVideoEncoder::~MobileVideoEncoder() {
	close();
}

bool MobileVideoEncoder::hardware() const {
	return !m_name.empty() && m_name != "libx264";
}

void MobileVideoEncoder::close() {
#ifdef USE_FFMPEG
	avcodec_free_context(&m_context);
	av_frame_free(&m_frame);
	av_packet_free(&m_packet);
	sws_freeContext(m_scaler);
	m_scaler = nullptr;
#endif
	m_name.clear();
	m_codec.clear();
}

bool MobileVideoEncoder::open(const VideoSize& source) {
	close();
	m_source = source;
	// The OpenGL renderer may already deliver an upscaled frame.
	m_size = source.width >= 480 ? source : VideoSize{ source.width * m_scale, source.height * m_scale };
#ifdef USE_FFMPEG
	std::string errors;
	while (!m_candidates.empty()) {
		const std::string name = m_candidates.front();
		const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
		AVPixelFormat format = codec ? pixelFormat(codec, name) : AV_PIX_FMT_NONE;
		int status = -1;
		if (codec && format != AV_PIX_FMT_NONE) {
			m_context = avcodec_alloc_context3(codec);
			m_context->width = m_size.width;
			m_context->height = m_size.height;
			m_context->pix_fmt = format;
			m_context->time_base = {1, 60};
			m_context->framerate = {60, 1};
			m_context->gop_size = 600;
			m_context->max_b_frames = 0;
			m_context->bit_rate = 1500000;
			m_context->rc_max_rate = 3000000;
			m_context->rc_buffer_size = 1000000;
			m_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
			// Full-range sRGB in, limited-range BT.601 out.
			m_context->color_range = AVCOL_RANGE_MPEG;
			m_context->colorspace = AVCOL_SPC_SMPTE170M;
			m_context->color_primaries = AVCOL_PRI_BT709;
			m_context->color_trc = AVCOL_TRC_IEC61966_2_1;
			tune(m_context, name);
			status = avcodec_open2(m_context, codec, nullptr);
		}
		if (status >= 0) {
			m_frame = av_frame_alloc();
			m_packet = av_packet_alloc();
			m_frame->format = format;
			m_frame->width = m_size.width;
			m_frame->height = m_size.height;
			m_scaler = sws_getContext(source.width, source.height, AV_PIX_FMT_RGB0, m_size.width, m_size.height,
			                          format, SWS_POINT, nullptr, nullptr, nullptr);
			if (m_scaler && format != AV_PIX_FMT_RGB0) {
				const int* matrix = sws_getCoefficients(SWS_CS_ITU601);
				sws_setColorspaceDetails(m_scaler, matrix, 1, matrix, 0, 0, 1 << 16, 1 << 16);
			}
			if (m_scaler && av_frame_get_buffer(m_frame, 0) >= 0) {
				m_name = name;
				m_needKey = true;
				m_error.clear();
				return true;
			}
		}
		if (!errors.empty()) errors += "; ";
		errors += name + ": " + (!codec ? std::string("not built in") :
		                         format == AV_PIX_FMT_NONE ? std::string("no usable pixel format") :
		                         status < 0 ? describe(status) : std::string("setup failed"));
		close();
		m_candidates.erase(m_candidates.begin());
	}
	m_error = errors.empty() ? std::string("No encoder configured for this codec") : errors;
#else
	m_error = "This build has no FFmpeg";
#endif
	return false;
}

bool MobileVideoEncoder::drain(std::vector<Packet>* packets) {
#ifdef USE_FFMPEG
	while (true) {
		const int status = avcodec_receive_packet(m_context, m_packet);
		if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return true;
		if (status < 0) {
			m_error = m_name + ": " + describe(status);
			return false;
		}
		Packet packet;
		packet.data.assign(m_packet->data, m_packet->data + m_packet->size);
		packet.key = m_packet->flags & AV_PKT_FLAG_KEY;
		av_packet_unref(m_packet);
		if (packet.key) {
			const bool hevc = m_context->codec_id == AV_CODEC_ID_HEVC;
			const int spsType = hevc ? 33 : 7;
			auto hasParameters = [&]() {
				return findNal(packet.data, spsType, hevc) >= 0 && findNal(packet.data, hevc ? 34 : 8, hevc) >= 0 &&
				    (!hevc || findNal(packet.data, 32, true) >= 0);
			};
			// A decoder joining at a keyframe needs all parameter sets in band.
			if (!hasParameters() && m_context->extradata_size > 0)
				packet.data.insert(packet.data.begin(), m_context->extradata, m_context->extradata + m_context->extradata_size);
			if (!hasParameters()) {
				m_error = m_name + ": keyframe is missing video parameter sets";
				return false;
			}
			const int sps = findNal(packet.data, spsType, hevc);
			if (hevc) {
				m_codec = hevcCodec(packet.data, sps);
			} else if (sps + 3 < int(packet.data.size())) {
				const uint8_t* bytes = packet.data.data() + sps;
				m_codec = format("avc1.%02x%02x%02x", bytes[1], bytes[2], bytes[3]);
			}
		}
		packets->push_back(std::move(packet));
	}
#else
	(void) packets;
	return false;
#endif
}

bool MobileVideoEncoder::encode(const VideoFrame& source, bool keyframe, std::vector<Packet>* packets) {
#ifdef USE_FFMPEG
	if (!source.pixels || source.width <= 0 || source.height <= 0) return true;
	const VideoSize size{ source.width, source.height };
	while (true) {
		if (!m_context || size != m_source) {
			if (!open(size)) return false;
		}
		bool ok = av_frame_make_writable(m_frame) >= 0;
		if (ok) {
			const uint8_t* planes[] = {source.pixels};
			const int strides[] = {source.stride};
			sws_scale(m_scaler, planes, strides, 0, source.height, m_frame->data, m_frame->linesize);
			m_frame->pts = m_pts++;
			const bool key = keyframe || m_needKey;
			m_frame->pict_type = key ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
#ifdef AV_FRAME_FLAG_KEY
			if (key) m_frame->flags |= AV_FRAME_FLAG_KEY;
			else m_frame->flags &= ~AV_FRAME_FLAG_KEY;
#else
			m_frame->key_frame = key;
#endif
			const int status = avcodec_send_frame(m_context, m_frame);
			if (status < 0) m_error = m_name + ": " + describe(status);
			ok = status >= 0 && drain(packets);
		}
		if (ok) {
			for (const Packet& packet : *packets) {
				if (packet.key) m_needKey = false;
			}
			return true;
		}
		// A hardware session can fail mid-stream (driver reset, session limit).
		// Drop it and continue with the next encoder from a fresh keyframe.
		packets->clear();
		m_candidates.erase(std::remove(m_candidates.begin(), m_candidates.end(), m_name), m_candidates.end());
		close();
	}
#else
	(void) source;
	(void) keyframe;
	(void) packets;
	m_error = "This build has no FFmpeg";
	return false;
#endif
}
