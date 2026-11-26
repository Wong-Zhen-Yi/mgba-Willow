/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MobilePairing.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <cstdlib>

using namespace QGBA;

static void _check(bool success, const char* message) {
	if (!success) {
		std::fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

int main(int argc, char** argv) {
	if (argc == 4) {
		// MobilePairing invokes this copy of the test executable in place of Node.js.
		QImage qr(1, 1, QImage::Format_RGB32);
		qr.fill(Qt::white);
		QBuffer png;
		_check(png.open(QIODevice::WriteOnly) && qr.save(&png, "PNG"), "Could not encode the fixture QR code");
		std::printf("{\"type\":\"ready\",\"origin\":\"https://willow.invalid\",\"link\":\"https://willow.invalid/#pair=abc\",\"qr\":\"%s\"}\n",
		            png.data().toBase64().constData());
		return 0;
	}

	QApplication app(argc, argv);
	QTemporaryDir fixture;
	_check(fixture.isValid(), "Could not create a temporary directory");
	const auto executable = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
	const QString suffix = ".exe";
#else
	const QString suffix;
#endif
	_check(QFile::copy(executable, fixture.filePath("node" + suffix)), "Could not install the fake Node executable");
	_check(QFile::copy(executable, fixture.filePath("tailscale" + suffix)),
	       "Could not install the fake Tailscale executable");
	QFile script(fixture.filePath("mobile-launch.mjs"));
	_check(script.open(QIODevice::WriteOnly), "Could not create the fixture script");
	script.close();
	qputenv("PATH", QDir::toNativeSeparators(fixture.path()).toLocal8Bit() + QDir::listSeparator().toLatin1() +
	                   qgetenv("PATH"));
	qputenv("WILLOW_MOBILE_SCRIPT", script.fileName().toUtf8());

	QWidget window;
	MobilePairing pairing(&window);
	pairing.enableTray();
	// Offscreen Linux has no system tray; the Settings summary reports the same states there.
	auto* tray = pairing.findChild<QSystemTrayIcon*>();
	_check(tray || !QSystemTrayIcon::isSystemTrayAvailable(), "The tray icon should exist when a tray is available");
	const auto is = [&](const char* state, const char* summary) {
		return tray ? tray->toolTip().endsWith(state) : pairing.summary().startsWith(summary);
	};
	_check(!tray || is("is starting", ""), "The tray should indicate that the server is starting");
	auto* link = pairing.findChild<QLineEdit*>();
	QPushButton* copy = nullptr;
	for (auto* button : pairing.findChildren<QPushButton*>()) {
		if (button->text() == "Copy Link")
			copy = button;
	}
	_check(link && copy, "The dialog should have a pairing link field and a Copy Link button");
	bool wasReady = false;
	bool copied = false;
	QObject::connect(&pairing, &MobilePairing::summaryChanged, &pairing, [&]() {
		if (!is("is ready", "On."))
			return;
		wasReady = true;
		_check(link->text() == "https://willow.invalid/#pair=abc", "The dialog should show the full pairing link");
		copy->click();
		copied = QApplication::clipboard()->text() == link->text() && copy->text() == "Copied";
	});
	pairing.start();
	QElapsedTimer timeout;
	timeout.start();
	while (!is("is unavailable", "Willow Mobile stopped.") && timeout.elapsed() < 5000) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	_check(wasReady, "The tray should indicate that the server is ready after its ready message");
	_check(is("is unavailable", "Willow Mobile stopped."), "The tray should clear readiness when the server exits");
	_check(copied, "Copy Link should put the full pairing link on the clipboard");
	_check(link->text().isEmpty(), "The pairing link should clear when the server exits");
	pairing.stop();
	_check(is("is off", "Off."), "The tray should indicate that the server is manually stopped");
	return 0;
}
