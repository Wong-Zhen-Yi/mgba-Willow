/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "GameLibrary.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#include <mgba-util/vfs.h>

#include <cstdio>
#include <cstdlib>

using namespace QGBA;

#define CHECK(condition)                                                      \
	do {                                                                      \
		if (!(condition)) {                                                   \
			std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
			std::exit(1);                                                     \
		}                                                                     \
	} while (0)

static void makeFile(const QString& path) {
	QFile file(path);
	CHECK(file.open(QIODevice::WriteOnly));
	file.write("synthetic fixture");
}

#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
static void makeZip(const QString& path, const QStringList& members) {
	VDir* archive = VDirOpenZip(path.toUtf8().constData(), O_CREAT | O_TRUNC | O_WRONLY);
	CHECK(archive);
	for (const QString& member : members) {
		VFile* file = archive->openFile(archive, member.toUtf8().constData(), O_CREAT | O_TRUNC | O_WRONLY);
		CHECK(file);
		QByteArray contents("synthetic fixture");
		CHECK(file->write(file, contents.constData(), contents.size()) == contents.size());
		CHECK(file->close(file));
	}
	CHECK(archive->close(archive));
}
#endif

static void waitForScan(bool& complete) {
	QElapsedTimer timer;
	timer.start();
	while (!complete && timer.elapsed() < 5000) {
		QApplication::processEvents();
		QThread::msleep(1);
	}
	CHECK(complete);
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	if (app.arguments().size() == 3 && app.arguments().at(1) == "--scan-folder") {
		// Read-only manual QA: discover cards without connecting any launch signal or loading a ROM.
		GameLibrary collection;
		bool complete = false;
		QObject::connect(&collection, &GameLibrary::scanFinished, &collection, [&]() { complete = true; });
		collection.setFolder(app.arguments().at(2));
		waitForScan(complete);
		auto* cards = collection.findChild<QListWidget*>("gameCards");
		std::printf("Discovered %d game cards\n", cards->count());
		for (int i = 0; i < cards->count(); ++i) {
			std::printf("%s [%s]\n", cards->item(i)->text().toUtf8().constData(),
			            cards->item(i)->data(Qt::UserRole + 1).toString().toUtf8().constData());
		}
		return 0;
	}
	QTemporaryDir temporary;
	CHECK(temporary.isValid());
	QString folder = temporary.path() + "/ROMs";
	CHECK(QDir().mkpath(folder + "/nested"));
	makeFile(folder + "/Emerald.GBA");
	makeFile(folder + "/Blue.gb");
	makeFile(folder + "/nested/Crystal.gbc");
	makeFile(folder + "/nested/Blue.gb");
	makeFile(folder + "/Advance.agb");
	makeFile(folder + "/Super.sgb");
	makeFile(folder + "/Emerald.sav");
	makeFile(folder + "/notes.txt");
	makeFile(folder + "/archive.zip");

	GameLibrary library;
	library.resize(1060, 700);
	library.show();
	auto* cards = library.findChild<QListWidget*>("gameCards");
	auto* search = library.findChild<QLineEdit*>("gameSearch");
	auto* play = library.findChild<QPushButton*>("play");
	auto* emptyTitle = library.findChild<QLabel*>("emptyTitle");
	CHECK(cards && search && play && emptyTitle);
	CHECK(!play->isEnabled());
	CHECK(emptyTitle->text() == "Your next adventure starts here");

	// Match Window's persistence contract, using an isolated on-disk settings file.
	QString settingsPath = temporary.path() + "/qt.ini";
	QSettings settings(settingsPath, QSettings::IniFormat);
	int folderChanges = 0;
	QObject::connect(&library, &GameLibrary::folderChanged, &library, [&](const QString& selected) {
		++folderChanges;
		settings.setValue(GameLibrary::folderSettingKey(), selected);
		settings.sync();
	});
	bool complete = false;
	QObject::connect(&library, &GameLibrary::scanFinished, &library, [&]() { complete = true; });
	library.setFolder(folder);
	waitForScan(complete);
	CHECK(folderChanges == 1);
	CHECK(cards->count() == 6);
	CHECK(cards->item(0)->text() == "Advance");
	CHECK(cards->item(1)->text() == "Blue");
	CHECK(cards->item(2)->text() == "Blue");
	CHECK(cards->item(1)->data(Qt::UserRole) != cards->item(2)->data(Qt::UserRole));
	if (app.arguments().size() == 3 && app.arguments().at(1) == "--screenshot") {
		QApplication::processEvents();
		CHECK(library.grab().save(app.arguments().at(2)));
	}

	QString launched;
	int launches = 0;
	QObject::connect(&library, &GameLibrary::gameSelected, &library, [&](const QString& path) {
		launched = path;
		++launches;
	});
	search->setText("eMeRaLd");
	int visible = 0;
	QListWidgetItem* emerald = nullptr;
	for (int i = 0; i < cards->count(); ++i) {
		if (!cards->item(i)->isHidden()) {
			++visible;
			emerald = cards->item(i);
		}
	}
	CHECK(visible == 1);
	cards->setCurrentItem(emerald);
	CHECK(play->isEnabled());
	play->click();
	CHECK(launches == 1);
	CHECK(launched == folder + "/Emerald.GBA");
	search->setText("no matching title");
	CHECK(!play->isEnabled());
	CHECK(emptyTitle->text() == "No matching games");
	cards->itemActivated(emerald);
	CHECK(launches == 1);
	search->clear();
	cards->itemActivated(emerald);
	CHECK(launches == 2);

	// Deleted ROMs cannot launch; refreshing drops stale cards.
	CHECK(QFile::remove(folder + "/Emerald.GBA"));
	complete = false;
	play->click();
	waitForScan(complete);
	CHECK(launches == 2);
	CHECK(cards->count() == 5);
	complete = false;
	library.setFolder(folder);
	waitForScan(complete);
	CHECK(folderChanges == 1);

	// A new instance restores the folder without asking the user again.
	QSettings restoredSettings(settingsPath, QSettings::IniFormat);
	GameLibrary restored;
	bool restoredComplete = false;
	QObject::connect(&restored, &GameLibrary::scanFinished, &restored, [&]() { restoredComplete = true; });
	restored.setFolder(restoredSettings.value(GameLibrary::folderSettingKey()).toString());
	waitForScan(restoredComplete);
	CHECK(restored.folder() == folder);
	CHECK(restored.findChild<QListWidget*>("gameCards")->count() == 5);

	// Unplugged/moved folders stay remembered and have a recovery screen.
	QString missing = temporary.path() + "/unplugged";
	library.setFolder(missing);
	CHECK(library.folder() == missing);
	CHECK(emptyTitle->text() == "ROM folder unavailable");
	CHECK(cards->count() == 0);

	// A folder switch while scanning must not leak entries from the old tree.
	QString other = temporary.path() + "/other";
	CHECK(QDir().mkpath(other));
	makeFile(other + "/Only.gb");
	library.setFolder(folder);
	complete = false;
	library.setFolder(other);
	waitForScan(complete);
	CHECK(cards->count() == 1);
	CHECK(cards->item(0)->text() == "Only");
	CHECK(library.folder() == other);

	// Refresh discovers newly added games without selecting the folder again.
	makeFile(other + "/New.gba");
	complete = false;
	library.refresh();
	waitForScan(complete);
	CHECK(cards->count() == 2);
	QString empty = temporary.path() + "/empty";
	CHECK(QDir().mkpath(empty));
	complete = false;
	library.setFolder(empty);
	waitForScan(complete);
	CHECK(cards->count() == 0);
	CHECK(emptyTitle->text() == "No games in this folder yet");
	CHECK(!play->isEnabled());
	// Compressed collections are scanned recursively and launch the archive itself.
	QString compressed = temporary.path() + "/Pokemon Rom Collection";
	CHECK(QDir().mkpath(compressed + "/nested"));
	CHECK(QDir().mkpath(compressed + "/__MACOSX/nested"));
	int archiveCount = 0;
#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
	makeZip(compressed + "/Compressed.ZIP", { "readme.html", "nested/Inside.GBA" });
	makeZip(compressed + "/nested/Collection.zip", { "First.gb", "games/Second.gbc" });
	makeZip(compressed + "/DS only.zip", { "DS/Game.nds", "readme.txt" });
	makeZip(compressed + "/Metadata only.zip", { "__MACOSX/._Phantom.gba", "._Phantom.gb" });
	makeFile(compressed + "/Corrupt.zip");
	CHECK(QFile::copy(compressed + "/Compressed.ZIP", compressed + "/._Compressed.zip"));
	CHECK(QFile::copy(compressed + "/Compressed.ZIP", compressed + "/__MACOSX/nested/Sidecar.zip"));
	archiveCount += 2;
#endif
#ifdef USE_LZMA
	// Tiny LZMA2 fixture containing nested/Seven.gbc, generated from synthetic text only.
	QFile seven(compressed + "/nested/Seven.7Z");
	CHECK(seven.open(QIODevice::WriteOnly));
	seven.write(QByteArray::fromBase64(
	    "N3q8ryccAATunsmUewAAAAAAAAAUAAAAAAAAAJWgS3kBABBzeW50aGV0aWMgZml4dHVyZQDgAGYAXl0AAIEzB64Pz0tvjAeww9oq11h"
	    "krLN4zlTdpEpvgnrmv8LWWiiEwnLef+TPezLsEBDUzGJoTTBRptr+NGfpj8sYPkWLP9cIPpJ0hhZj/JtQ0ERRQzW9a03TJj3/"
	    "40AAAAAXBhUBCWYABwsBAAEhIQEYDGcAAA=="));
	seven.close();
	++archiveCount;
#endif
	complete = false;
	library.setFolder(compressed);
	waitForScan(complete);
	CHECK(cards->count() == archiveCount);
#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
	search->setText("Compressed");
	QListWidgetItem* zipped = nullptr;
	for (int i = 0; i < cards->count(); ++i) {
		if (!cards->item(i)->isHidden()) {
			CHECK(!zipped);
			zipped = cards->item(i);
		}
	}
	CHECK(zipped);
	CHECK(zipped->data(Qt::UserRole + 1).toString() == "ZIP");
	// Accessibility APIs can select a card without setting the keyboard current item.
	zipped->setSelected(true);
	CHECK(play->isEnabled());
	int previousLaunches = launches;
	play->click();
	CHECK(launches == previousLaunches + 1);
	CHECK(launched == compressed + "/Compressed.ZIP");
	CHECK(!QFile::exists(compressed + "/Inside.GBA"));
	CHECK(!QFile::exists(compressed + "/nested/Inside.GBA"));
	CHECK(QFile::remove(compressed + "/Compressed.ZIP"));
	complete = false;
	play->click();
	waitForScan(complete);
	CHECK(launches == previousLaunches + 1);
	CHECK(cards->count() == archiveCount - 1);
	search->clear();
#endif
	library.setFolder(QString());
	CHECK(cards->count() == 0);
	CHECK(emptyTitle->text() == "Your next adventure starts here");
	std::puts("Game library tests passed");
	return 0;
}
