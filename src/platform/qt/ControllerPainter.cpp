/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "ControllerPainter.h"

#include <QMutexLocker>
#include <QPainter>
#include <QDateTime>
#include <QFontDatabase>
#include <algorithm>

using namespace QGBA;

void ControllerPainter::addAIInteraction(const QString& text) {
	QMutexLocker lock(&m_mutex);
	m_aiInteractions.append(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text.left(2048));
	while (m_aiInteractions.size() > 32) m_aiInteractions.removeFirst();
}

void ControllerPainter::setKeys(unsigned keys) {
	QMutexLocker lock(&m_mutex);
	const auto until = Clock::now() + std::chrono::milliseconds(100);
	for (unsigned i = 0; i < m_pressedUntil.size(); ++i) {
		if ((keys & ~m_keys) & (1U << i)) m_pressedUntil[i] = until;
	}
	m_keys = keys;
}

void ControllerPainter::reset() {
	QMutexLocker lock(&m_mutex);
	m_keys = 0;
	m_pressedUntil.fill(Clock::time_point{});
	m_viewport = {};
}

void ControllerPainter::setViewport(const QRect& viewport) {
	QMutexLocker lock(&m_mutex);
	m_viewport = viewport;
}

bool ControllerPainter::isVisible(const QSize& size) const {
	QMutexLocker lock(&m_mutex);
	return m_viewport.isValid() && size.width() >= 240 &&
		(m_viewport.top() >= 64 || size.height() - m_viewport.bottom() - 1 >= 64);
}

void ControllerPainter::paint(QPainter* painter, const QSize& size) {
	unsigned keys;
	QRect viewport;
	QStringList interactions;
	{
		QMutexLocker lock(&m_mutex);
		keys = m_keys;
		viewport = m_viewport;
		interactions = m_aiInteractions;
		const auto now = Clock::now();
		for (unsigned i = 0; i < m_pressedUntil.size(); ++i) {
			if (now < m_pressedUntil[i]) keys |= 1U << i;
		}
	}
	// Draw only in the unused top letterbox; never obscure game pixels.
	if (viewport.isValid() && viewport.top() >= 64 && size.width() >= 240) {
		const QRect panel(12, 8, size.width() - 24, viewport.top() - 16);
		painter->save();
		painter->setClipRect(panel);
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setPen(QPen(QColor("#343d4b"), 1));
		painter->setBrush(QColor("#151a22"));
		painter->drawRoundedRect(panel, 8, 8);
		QFont font = painter->font();
		font.setPixelSize(12);
		font.setBold(true);
		painter->setFont(font);
		painter->setPen(QColor("#97f9db"));
		painter->drawText(panel.adjusted(12, 6, -12, 0), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("AI / MCP INPUTS"));
		font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		font.setPixelSize(12);
		painter->setFont(font);
		const int lineHeight = painter->fontMetrics().height() + 4;
		const int rows = std::max(0, (panel.height() - 34) / lineHeight);
		painter->setPen(QColor("#b9c4d3"));
		if (interactions.isEmpty()) interactions.append(QStringLiteral("Waiting for AI / MCP interactions..."));
		const int first = std::max(0, int(interactions.size()) - rows);
		for (int i = first; i < interactions.size(); ++i) {
			const QString line = painter->fontMetrics().elidedText(interactions[i], Qt::ElideRight, panel.width() - 24);
			painter->drawText(QRect(panel.x() + 12, panel.y() + 28 + (i - first) * lineHeight,
				panel.width() - 24, lineHeight), Qt::AlignLeft | Qt::AlignVCenter, line);
		}
		painter->restore();
	}
	const QRect band(0, viewport.bottom() + 1, size.width(), size.height() - viewport.bottom() - 1);
	if (!viewport.isValid() || band.height() < 64 || band.width() < 240) return;
	const qreal scale = std::min({qreal(1.25), (band.width() - 24) / 420., (band.height() - 16) / 112.});
	painter->save();
	painter->setClipRect(band);
	painter->setRenderHint(QPainter::Antialiasing);
	painter->translate(band.center().x() - 210 * scale, band.center().y() - 56 * scale);
	painter->scale(scale, scale);
	painter->setPen(QPen(QColor("#343d4b"), 1.5));
	painter->setBrush(QColor("#151a22"));
	painter->drawRoundedRect(QRectF(0, 8, 420, 104), 38, 38);
	QFont font = painter->font();
	font.setPixelSize(12);
	font.setBold(true);
	painter->setFont(font);
	const auto button = [&](const QRectF& rect, unsigned bit, const QString& label, bool round = false) {
		const bool down = keys & (1U << bit);
		painter->setPen(QPen(down ? QColor("#97f9db") : QColor("#455165"), down ? 2 : 1));
		painter->setBrush(down ? QColor("#36d6a6") : QColor("#252e3b"));
		if (round) painter->drawEllipse(rect);
		else painter->drawRoundedRect(rect, 5, 5);
		painter->setPen(down ? QColor("#08261d") : QColor("#b9c4d3"));
		painter->drawText(rect, Qt::AlignCenter, label);
	};
	button(QRectF(26, 0, 82, 19), 9, QStringLiteral("L"));
	button(QRectF(312, 0, 82, 19), 8, QStringLiteral("R"));
	button(QRectF(58, 27, 26, 26), 6, QStringLiteral("\u25b2"));
	button(QRectF(58, 79, 26, 26), 7, QStringLiteral("\u25bc"));
	button(QRectF(32, 53, 26, 26), 5, QStringLiteral("\u25c0"));
	button(QRectF(84, 53, 26, 26), 4, QStringLiteral("\u25b6"));
	painter->setPen(Qt::NoPen);
	painter->setBrush(QColor("#252e3b"));
	painter->drawRect(QRectF(58, 53, 26, 26));
	button(QRectF(151, 70, 52, 18), 2, QStringLiteral("SELECT"));
	button(QRectF(216, 70, 52, 18), 3, QStringLiteral("START"));
	button(QRectF(302, 61, 37, 37), 1, QStringLiteral("B"), true);
	button(QRectF(352, 35, 37, 37), 0, QStringLiteral("A"), true);
	font.setPixelSize(10);
	font.setBold(false);
	painter->setFont(font);
	painter->setPen(QColor("#8592a4"));
	painter->drawText(QRectF(135, 32, 150, 20), Qt::AlignCenter, QStringLiteral("LIVE INPUT"));
	painter->restore();
}
