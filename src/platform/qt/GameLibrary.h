/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QImage>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <memory>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace Willow::Library {
class Scanner;
}

namespace QGBA {

// The library is independent of the optional SQLite ROM catalog.
class GameLibrary : public QWidget {
	Q_OBJECT

public:
	explicit GameLibrary(QWidget* parent = nullptr);
	~GameLibrary() override;

	// A library game as remote controllers see it; the id hides the file path.
	struct Entry {
		QString id;
		QString title;
		QString platform;
		bool artwork = false;
	};

	QString folder() const { return m_folder; }
	QSize sizeHint() const override { return QSize(1060, 700); }
	static QString folderSettingKey() { return QStringLiteral("romLibraryFolder"); }
	static QString gameId(const QString& path);

	QList<Entry> entries() const;
	QImage artwork(const QString& id, int width) const;
	bool scanning() const { return bool(m_scanner); }
	// Emits gameSelected for a listed game that still exists on disk.
	bool launch(const QString& id);

public slots:
	void setFolder(const QString& folder);
	void chooseFolder();
	void refresh();

signals:
	void folderChanged(const QString& folder);
	void gameSelected(const QString& path);
	void openRequested();
	void mobilePairingRequested();
	void settingsRequested();
	void scanFinished();

private:
	void scanBatch();
	void filterGames();
	void updateStatus();
	void launchSelected();
	void loadArtwork();

	QString m_folder;
	QTimer m_scanTimer;
	std::unique_ptr<Willow::Library::Scanner> m_scanner;
	QListWidget* m_games;
	QLineEdit* m_search;
	QLabel* m_count;
	QLabel* m_folderLabel;
	QLabel* m_emptyTitle;
	QLabel* m_emptyText;
	QStackedWidget* m_pages;
	QPushButton* m_play;
	QPushButton* m_emptyChoose;
	QNetworkAccessManager* m_artworkNetwork;
	QQueue<QString> m_artworkQueue;
	QSet<QString> m_requestedArtwork;
	int m_activeArtwork = 0;
};

}
