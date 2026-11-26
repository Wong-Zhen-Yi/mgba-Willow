/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace QGBA {

// Low-latency HEVC via NVIDIA NVENC, or H.264 with hardware encoders
// preferred over x264. Native-size frames are
// scaled by an integer factor with nearest-neighbour sampling so 4:2:0 chroma
// never spans two console pixels; frames 480 or more pixels wide pass unscaled.
// Output is Annex B with in-band parameter sets and no B-frames, which
// WebCodecs can decode without extra configuration.
class MobileVideoEncoder {
public:
	struct Packet {
		QByteArray data;
		bool key = false;
	};

	static bool available();
	static QStringList defaultEncoders(bool hevc = false);

	explicit MobileVideoEncoder(QStringList encoders = defaultEncoders(), int scale = 2);
	~MobileVideoEncoder();
	MobileVideoEncoder(const MobileVideoEncoder&) = delete;
	MobileVideoEncoder& operator=(const MobileVideoEncoder&) = delete;

	// Encodes one RGBX frame and appends any finished access units. Returns false
	// once every candidate encoder has failed; error() then explains why.
	bool encode(const QImage& frame, bool keyframe, QList<Packet>* packets);

	QString encoder() const { return m_name; }
	bool hardware() const;
	// RFC 6381 codec string, such as "avc1.4d401e", known after the first keyframe.
	QString codec() const { return m_codec; }
	QSize size() const { return m_size; }
	QString error() const { return m_error; }

private:
	bool open(const QSize& source);
	void close();
	bool drain(QList<Packet>* packets);

	QStringList m_candidates;
	int m_scale;
	QString m_name;
	QString m_codec;
	QString m_error;
	QSize m_source;
	QSize m_size;
	qint64 m_pts = 0;
	bool m_needKey = true;
	AVCodecContext* m_context = nullptr;
	AVFrame* m_frame = nullptr;
	AVPacket* m_packet = nullptr;
	SwsContext* m_scaler = nullptr;
};

}
