/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../ControllerPainter.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cstdlib>
#include <iostream>

#define CHECK(condition)                                                           \
	do {                                                                           \
		if (!(condition)) {                                                        \
			std::cerr << "Failed line " << __LINE__ << ": " << #condition << '\n'; \
			std::abort();                                                          \
		}                                                                          \
	} while (0)

int main(int argc, char** argv) {
	QGuiApplication app(argc, argv);
	QGBA::ControllerPainter overlay;
	const QSize size(960, 1000);
	overlay.setViewport(QRect(0, 160, 960, 640));
	const auto render = [&]() {
		QImage image(size, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::black);
		QPainter painter(&image);
		overlay.paint(&painter, size);
		return image;
	};
	const QImage empty = render();
	overlay.setObjective(QStringLiteral("Heal the party\nThen challenge the Gym"));
	const QImage updated = render();
	CHECK(empty != updated);
	// Objective changes must stay inside the bottom-right panel.
	CHECK(empty.copy(0, 0, 960, 800) == updated.copy(0, 0, 960, 800));
	CHECK(empty.copy(0, 800, 550, 200) == updated.copy(0, 800, 550, 200));
	overlay.setObjective(QString(512, QChar('W')));
	const QImage longText = render();
	CHECK(empty.copy(0, 0, 960, 800) == longText.copy(0, 0, 960, 800));
	CHECK(empty.copy(0, 800, 550, 200) == longText.copy(0, 800, 550, 200));
	CHECK(empty.copy(0, 970, 960, 30) == longText.copy(0, 970, 960, 30));
	overlay.setObjective(QString());
	CHECK(empty == render());
	return 0;
}
