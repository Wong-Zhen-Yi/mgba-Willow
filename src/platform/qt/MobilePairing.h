/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QDialog>
#include <QProcess>
#include <QTimer>

class QAction;
class QLabel;
class QPushButton;
class QSystemTrayIcon;

namespace QGBA {

class MobilePairing : public QDialog {
	Q_OBJECT

public:
	explicit MobilePairing(QWidget* parent);
	~MobilePairing() override;

	void start();
	void stop();
	void open();
	void enableTray();

	// One line for Settings: what Willow Mobile is doing now.
	QString summary() const { return m_summary; }

signals:
	void summaryChanged(const QString& summary);

private:
	enum class Status { OFF, STARTING, READY, UNAVAILABLE };

	void setStatus(Status status, const QString& text, const QString& summary = { });
	void updateTray();
	void readUpdates();
	void clearPairing();
	void scheduleRetry();

	QProcess m_process;
	QTimer m_retry;
	int m_retryDelay = 0;
	QByteArray m_updates;
	QString m_summary;
	QLabel* m_status;
	QLabel* m_qr;
	QPushButton* m_start;
	QPushButton* m_stop;
	QSystemTrayIcon* m_tray = nullptr;
	QAction* m_trayStatus = nullptr;
	Status m_state = Status::STARTING;
	bool m_stopping = false;
	bool m_reportedError = false;
};

}
