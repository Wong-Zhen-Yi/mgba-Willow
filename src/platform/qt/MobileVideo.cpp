/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MobileVideo.h"

#include <mgba/flags.h>

#ifdef USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

using namespace QGBA;

namespace {

// Finds the NAL unit of the given type in an Annex B buffer; returns its payload offset or -1.
int findNal(const QByteArray& data, int type) {
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData());
	for (int i = 0; i + 3 < data.size(); ++i) {
		if (bytes[i] || bytes[i + 1] || bytes[i + 2] != 1) continue;
		if ((bytes[i + 3] & 0x1F) == type) return i + 3;
		i += 2;
	}
	return -1;
}

#ifdef USE_FFMPEG
void setOption(AVCodecContext* context, const char* key, const char* value) {
	// Each encoder names its options differently; unknown ones are ignored.
	av_opt_set(context->priv_data, key, value, 0);
}

void tune(AVCodecContext* context, const QString& name) {
	if (name == "h264_nvenc") {
		setOption(context, "preset", "p2");
		setOption(context, "tune", "ull");
		setOption(context, "rc", "vbr");
		setOption(context, "zerolatency", "1");
		setOption(context, "delay", "0");
		setOption(context, "rc-lookahead", "0");
		setOption(context, "forced-idr", "1");
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

AVPixelFormat pixelFormat(const AVCodec* codec, const QString& name) {
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
	QList<AVPixelFormat> preferred{AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P};
	if (name == "h264_nvenc") preferred.prepend(AV_PIX_FMT_RGB0);
	if (!formats) return AV_PIX_FMT_YUV420P;
	for (AVPixelFormat format : preferred) {
		for (const AVPixelFormat* supported = formats; *supported != AV_PIX_FMT_NONE; ++supported) {
			if (*supported == format) return format;
		}
	}
	return AV_PIX_FMT_NONE;
}

QString describe(int error) {
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(error, buffer, sizeof(buffer));
	return QString::fromLocal8Bit(buffer);
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

QStringList MobileVideoEncoder::defaultEncoders() {
	// WILLOW_MOBILE_ENCODER=libx264 (or a comma-separated list) overrides the order.
	const QString forced = qEnvironmentVariable("WILLOW_MOBILE_ENCODER").trimmed();
	if (!forced.isEmpty()) return forced.split(',', Qt::SkipEmptyParts);
	return {"h264_nvenc", "h264_amf", "h264_qsv", "h264_mf", "libx264"};
}

MobileVideoEncoder::MobileVideoEncoder(QStringList encoders, int scale)
	: m_candidates(encoders)
	, m_scale(qBound(1, scale, 4)) {
	for (QString& name : m_candidates) name = name.trimmed();
}

MobileVideoEncoder::~MobileVideoEncoder() {
	close();
}

bool MobileVideoEncoder::hardware() const {
	return !m_name.isEmpty() && m_name != "libx264";
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

bool MobileVideoEncoder::open(const QSize& source) {
	close();
	m_source = source;
	// The OpenGL renderer may already deliver an upscaled frame.
	m_size = source.width() >= 480 ? source : source * m_scale;
#ifdef USE_FFMPEG
	QStringList errors;
	while (!m_candidates.isEmpty()) {
		const QString name = m_candidates.first();
		const AVCodec* codec = avcodec_find_encoder_by_name(name.toLatin1().constData());
		AVPixelFormat format = codec ? pixelFormat(codec, name) : AV_PIX_FMT_NONE;
		int status = -1;
		if (codec && format != AV_PIX_FMT_NONE) {
			m_context = avcodec_alloc_context3(codec);
			m_context->width = m_size.width();
			m_context->height = m_size.height();
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
			m_frame->width = m_size.width();
			m_frame->height = m_size.height();
			m_scaler = sws_getContext(source.width(), source.height(), AV_PIX_FMT_RGB0, m_size.width(), m_size.height(),
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
		errors.append(QStringLiteral("%1: %2").arg(name, !codec ? QStringLiteral("not built in") :
		                                                 format == AV_PIX_FMT_NONE ? QStringLiteral("no usable pixel format") :
		                                                 status < 0 ? describe(status) : QStringLiteral("setup failed")));
		close();
		m_candidates.removeFirst();
	}
	m_error = errors.isEmpty() ? QStringLiteral("No H.264 encoder") : errors.join("; ");
#else
	m_error = QStringLiteral("This build has no FFmpeg");
#endif
	return false;
}

bool MobileVideoEncoder::drain(QList<Packet>* packets) {
#ifdef USE_FFMPEG
	while (true) {
		const int status = avcodec_receive_packet(m_context, m_packet);
		if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return true;
		if (status < 0) {
			m_error = QStringLiteral("%1: %2").arg(m_name, describe(status));
			return false;
		}
		Packet packet;
		packet.data = QByteArray(reinterpret_cast<const char*>(m_packet->data), m_packet->size);
		packet.key = m_packet->flags & AV_PKT_FLAG_KEY;
		av_packet_unref(m_packet);
		if (packet.key) {
			// Some encoders keep SPS/PPS only in extradata; a decoder joining at this
			// keyframe needs them in band.
			if (findNal(packet.data, 7) < 0 && m_context->extradata_size > 0) {
				packet.data.prepend(reinterpret_cast<const char*>(m_context->extradata), m_context->extradata_size);
			}
			const int sps = findNal(packet.data, 7);
			if (sps >= 0 && sps + 3 < packet.data.size()) {
				const auto* bytes = reinterpret_cast<const uchar*>(packet.data.constData()) + sps;
				m_codec = QString::asprintf("avc1.%02x%02x%02x", bytes[1], bytes[2], bytes[3]);
			}
		}
		packets->append(packet);
	}
#else
	Q_UNUSED(packets);
	return false;
#endif
}

bool MobileVideoEncoder::encode(const QImage& frame, bool keyframe, QList<Packet>* packets) {
#ifdef USE_FFMPEG
	if (frame.isNull()) return true;
	const QImage source = frame.format() == QImage::Format_RGBX8888 ? frame :
	                                                                  frame.convertToFormat(QImage::Format_RGBX8888);
	while (true) {
		if (!m_context || source.size() != m_source) {
			if (!open(source.size())) return false;
		}
		bool ok = av_frame_make_writable(m_frame) >= 0;
		if (ok) {
			const uint8_t* planes[] = {source.constBits()};
			const int strides[] = {int(source.bytesPerLine())};
			sws_scale(m_scaler, planes, strides, 0, source.height(), m_frame->data, m_frame->linesize);
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
			if (status < 0) m_error = QStringLiteral("%1: %2").arg(m_name, describe(status));
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
		m_candidates.removeAll(m_name);
		close();
	}
#else
	Q_UNUSED(frame);
	Q_UNUSED(keyframe);
	Q_UNUSED(packets);
	m_error = QStringLiteral("This build has no FFmpeg");
	return false;
#endif
}
