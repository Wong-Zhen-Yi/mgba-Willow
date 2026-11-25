/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QDialog>
#include <QProcess>

class QLabel;
class QPushButton;

namespace QGBA {

class MobilePairing : public QDialog {
	Q_OBJECT

public:
	explicit MobilePairing(QWidget* parent);
	~MobilePairing() override;

	void start();
	void stop();

private:
	void readUpdates();
	void clearPairing();

	QProcess m_process;
	QByteArray m_updates;
	QLabel* m_status;
	QLabel* m_qr;
	QPushButton* m_start;
	QPushButton* m_stop;
	bool m_stopping = false;
	bool m_reportedError = false;
};

}
