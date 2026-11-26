/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MobilePairing.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

using namespace QGBA;

namespace {

QString findProgram(const QString& name, const QString& installedPath) {
	auto path = QStandardPaths::findExecutable(name);
	if (path.isEmpty() && !installedPath.isEmpty() && QFileInfo(installedPath).isExecutable())
		return installedPath;
	return path;
}

}

MobilePairing::MobilePairing(QWidget* parent)
    : QDialog(parent) {
	setWindowTitle(tr("Pair iPhone"));
	setMinimumWidth(440);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(22, 20, 22, 18);
	layout->setSpacing(12);

	m_status = new QLabel(this);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);

	m_qr = new QLabel(this);
	m_qr->setAccessibleName(tr("Willow Mobile pairing QR code"));
	layout->addWidget(m_qr, 0, Qt::AlignHCenter);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	m_start = buttons->addButton(tr("Start"), QDialogButtonBox::ActionRole);
	m_stop = buttons->addButton(tr("Stop"), QDialogButtonBox::ActionRole);
	layout->addWidget(buttons);
	clearPairing();
	m_stop->setEnabled(false);
	connect(m_start, &QPushButton::clicked, this, &MobilePairing::start);
	connect(m_stop, &QPushButton::clicked, this, &MobilePairing::stop);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	// Serving starts with Willow, so retry quietly when Tailscale is not ready yet.
	m_retry.setSingleShot(true);
	connect(&m_retry, &QTimer::timeout, this, &MobilePairing::start);

	connect(&m_process, &QProcess::readyReadStandardOutput, this, &MobilePairing::readUpdates);
	// Keep the QR in the GUI process, away from the launcher output.
	m_process.setStandardErrorFile(QProcess::nullDevice());
	connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		setStatus(tr("Could not start Willow Mobile. Check your Node.js installation and try again."));
		m_start->setEnabled(true);
		m_stop->setEnabled(false);
	});
	connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this]() {
		clearPairing();
		m_start->setEnabled(true);
		m_stop->setEnabled(false);
		if (m_stopping)
			return;
		if (!m_reportedError)
			setStatus(tr("Willow Mobile stopped."));
		scheduleRetry();
	});
}

MobilePairing::~MobilePairing() {
	m_process.closeWriteChannel();
	if (m_process.state() != QProcess::NotRunning && !m_process.waitForFinished(2000)) {
		m_process.kill();
		m_process.waitForFinished(500);
	}
}

void MobilePairing::start() {
	m_retry.stop();
	if (m_process.state() != QProcess::NotRunning)
		return;
	const auto script = qEnvironmentVariable("WILLOW_MOBILE_SCRIPT");
	if (!QFileInfo(script).isFile()) {
		setStatus(tr("Open Willow using Launch mGBA.cmd to set up iPhone pairing."));
		return;
	}
	const auto programFiles = qEnvironmentVariable("ProgramFiles");
	const auto node = findProgram("node", QDir(programFiles).filePath("nodejs/node.exe"));
	const auto tailscale = findProgram("tailscale", QDir(programFiles).filePath("Tailscale/tailscale.exe"));
	if (node.isEmpty() || tailscale.isEmpty()) {
		setStatus(node.isEmpty() ? tr("Install Node.js 20 or newer, then reopen Willow.") :
		                           tr("Install and connect Tailscale on this PC, then try again."));
		return;
	}
	m_stopping = false;
	m_reportedError = false;
	m_updates.clear();
	setStatus(tr("Starting Willow Mobile…"));
	m_start->setEnabled(false);
	m_stop->setEnabled(true);
	m_process.start(node, { script, QString::number(QCoreApplication::applicationPid()), tailscale });
}

void MobilePairing::stop() {
	m_stopping = true;
	m_retry.stop();
	m_retryDelay = 0;
	clearPairing();
	m_stop->setEnabled(false);
	setStatus(tr("Willow Mobile is off. Choose Start to pair again."), tr("Off."));
	m_process.closeWriteChannel();
}

void MobilePairing::setStatus(const QString& text, const QString& summary) {
	m_status->setText(text);
	const auto line = summary.isEmpty() ? text : summary;
	if (line == m_summary)
		return;
	m_summary = line;
	emit summaryChanged(m_summary);
}

void MobilePairing::scheduleRetry() {
	// 30 s, then doubling up to 5 min: one short Node run per attempt, nothing in between.
	m_retryDelay = m_retryDelay ? qMin(m_retryDelay * 2, 300) : 30;
	const auto retry = tr("Willow will try again in %n second(s).", nullptr, m_retryDelay);
	setStatus(QString("%1\n%2").arg(m_status->text(), retry), QString("%1 %2").arg(m_summary, retry));
	m_retry.start(m_retryDelay * 1000);
}

void MobilePairing::clearPairing() {
	m_qr->clear();
	m_qr->hide();
}

void MobilePairing::readUpdates() {
	const auto bytes = m_process.readAllStandardOutput();
	if (m_stopping)
		return;
	m_updates += bytes;
	if (m_updates.size() > 128 * 1024) {
		stop();
		setStatus(tr("Willow Mobile returned an invalid pairing response."));
		return;
	}
	int end;
	while ((end = m_updates.indexOf('\n')) >= 0) {
		const auto update = QJsonDocument::fromJson(m_updates.left(end)).object();
		m_updates.remove(0, end + 1);
		const auto type = update.value("type").toString();
		if (type == "error" || type == "progress") {
			m_reportedError = type == "error";
			setStatus(update.value("message").toString());
		} else if (type == "ready") {
			QPixmap qr;
			if (!qr.loadFromData(QByteArray::fromBase64(update.value("qr").toString().toLatin1()), "PNG")) {
				stop();
				setStatus(tr("Could not load the pairing QR code."));
				return;
			}
			// Funnel links are public, so the iPhone does not need Tailscale for them.
			const auto steps = update.value("access").toString() == "funnel" ?
			    tr("Scan this code with your iPhone Camera. Keep it private: anyone with the link can pair.") :
			    tr("Connect your iPhone to Tailscale, then scan this code with Camera.");
			const auto origin = update.value("origin").toString();
			setStatus(QString("%1\n%2").arg(steps, origin), tr("On. Your iPhone can pair at %1").arg(origin));
			m_retryDelay = 0;
			m_qr->setPixmap(qr);
			m_qr->show();
			adjustSize();
		}
	}
}
