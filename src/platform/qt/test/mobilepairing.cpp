/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MobilePairing.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
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
		std::printf("{\"type\":\"ready\",\"origin\":\"https://willow.invalid\",\"qr\":\"%s\"}\n",
		            png.data().toBase64().constData());
		return 0;
	}

	QApplication app(argc, argv);
	QTemporaryDir fixture;
	_check(fixture.isValid(), "Could not create a temporary directory");
	const auto executable = QCoreApplication::applicationFilePath();
	_check(QFile::copy(executable, fixture.filePath("node.exe")), "Could not install the fake Node executable");
	_check(QFile::copy(executable, fixture.filePath("tailscale.exe")),
	       "Could not install the fake Tailscale executable");
	QFile script(fixture.filePath("mobile-launch.mjs"));
	_check(script.open(QIODevice::WriteOnly), "Could not create the fixture script");
	script.close();
	qputenv("PATH", QDir::toNativeSeparators(fixture.path()).toLocal8Bit() + ';' + qgetenv("PATH"));
	qputenv("WILLOW_MOBILE_SCRIPT", script.fileName().toUtf8());

	QWidget window;
	MobilePairing pairing(&window);
	pairing.enableTray();
	auto* tray = pairing.findChild<QSystemTrayIcon*>();
	_check(tray && tray->toolTip().endsWith("is starting"), "The tray should indicate that the server is starting");
	bool wasReady = false;
	QObject::connect(&pairing, &MobilePairing::summaryChanged, &pairing,
	                 [&]() { wasReady |= tray->toolTip().endsWith("is ready"); });
	pairing.start();
	QElapsedTimer timeout;
	timeout.start();
	while (!tray->toolTip().endsWith("is unavailable") && timeout.elapsed() < 5000) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	_check(wasReady, "The tray should indicate that the server is ready after its ready message");
	_check(tray->toolTip().endsWith("is unavailable"), "The tray should clear readiness when the server exits");
	pairing.stop();
	_check(tray->toolTip().endsWith("is off"), "The tray should indicate that the server is manually stopped");
	return 0;
}
