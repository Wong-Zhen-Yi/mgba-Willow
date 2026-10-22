/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QTimer>
#include <QWidget>

#include <memory>

class QDirIterator;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace QGBA {

// The library is independent of the optional SQLite ROM catalog.
class GameLibrary : public QWidget {
	Q_OBJECT

public:
	explicit GameLibrary(QWidget* parent = nullptr);
	~GameLibrary() override;

	QString folder() const { return m_folder; }
	QSize sizeHint() const override { return QSize(1060, 700); }
	static QString folderSettingKey() { return QStringLiteral("romLibraryFolder"); }

public slots:
	void setFolder(const QString& folder);
	void chooseFolder();
	void refresh();

signals:
	void folderChanged(const QString& folder);
	void gameSelected(const QString& path);
	void openRequested();
	void settingsRequested();
	void scanFinished();

private:
	void scanBatch();
	void filterGames();
	void updateStatus();
	void launchSelected();

	QString m_folder;
	QTimer m_scanTimer;
	std::unique_ptr<QDirIterator> m_iterator;
	QListWidget* m_games;
	QLineEdit* m_search;
	QLabel* m_count;
	QLabel* m_folderLabel;
	QLabel* m_emptyTitle;
	QLabel* m_emptyText;
	QStackedWidget* m_pages;
	QPushButton* m_play;
	QPushButton* m_emptyChoose;
};

}
