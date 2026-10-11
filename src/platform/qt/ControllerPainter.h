/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QMutex>
#include <QRect>
#include <QStringList>
#include <array>
#include <chrono>

class QPainter;

namespace QGBA {

// Shared by the GUI and OpenGL rendering threads. Input edge notifications
// preserve one-frame taps even when fast-forward skips their video frames.
class ControllerPainter {
public:
	void setKeys(unsigned keys);
	void addAIInteraction(const QString& text);
	void reset();
	void setViewport(const QRect& viewport);
	bool isVisible(const QSize& size) const;
	void paint(QPainter* painter, const QSize& size);

private:
	using Clock = std::chrono::steady_clock;
	mutable QMutex m_mutex;
	unsigned m_keys = 0;
	std::array<Clock::time_point, 10> m_pressedUntil{};
	QRect m_viewport;
	QStringList m_aiInteractions;
};

}
