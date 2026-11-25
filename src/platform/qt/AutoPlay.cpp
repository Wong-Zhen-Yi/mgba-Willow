/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "AutoPlay.h"

#include "ActionMapper.h"
#include "ConfigController.h"
#include "CoreController.h"
#include "Window.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>

using namespace QGBA;

namespace {

QString findProgram(const QString& name, const QString& installedPath) {
	if (!installedPath.isEmpty() && QFileInfo(installedPath).isExecutable())
		return installedPath;
	return QStandardPaths::findExecutable(name);
}

QString runnerScript() {
	const auto script = qEnvironmentVariable("WILLOW_AUTOPLAY_SCRIPT");
	if (QFileInfo(script).isFile())
		return script;
	// Older launchers only set the mobile script, which lives beside the runner.
	const auto mobile = qEnvironmentVariable("WILLOW_MOBILE_SCRIPT");
	const auto sibling = mobile.isEmpty() ? QString() : QFileInfo(mobile).dir().filePath("autoplay.mjs");
	return QFileInfo(sibling).isFile() ? sibling : QString();
}

}

AutoPlay::AutoPlay(Window* window, ActionMapper* actions, const QString& sessionId)
    : QObject(window)
    , m_window(window)
    , m_sessionId(sessionId) {
	// Defer so the action finishes updating its own check state before start() or stop() syncs it.
	m_action = actions->addBooleanAction(
	    tr("Auto Play"), "autoPlay",
	    [this](bool active) { QTimer::singleShot(0, this, [this, active]() { active ? start() : stop(); }); }, "ai");

	actions->addMenu(tr("Auto Play Model"), "autoPlayModel", "ai");
	auto* model = window->config()->addOption("autoPlayModel");
	model->addValue(tr("GPT-6.1 Terra (medium)"), "terra", actions, "autoPlayModel");
	model->addValue(tr("GPT-6 Luna (extra high)"), "luna", actions, "autoPlayModel");
	model->connect(
	    [this](const QVariant&) {
		    m_window->config()->write();
		    if (m_process.state() != QProcess::NotRunning)
			    showMessage(tr("Auto Play: the new model applies the next time you start it."));
	    },
	    this);
	const auto initial = window->config()->getOption("autoPlayModel", "terra");
	window->config()->setOption("autoPlayModel", initial == "luna" ? "luna" : "terra");

	m_button = new QToolButton(window);
	m_button->setCheckable(true);
	m_button->setAutoRaise(true);
	m_button->setText(tr("▶ Auto Play"));
	m_button->setToolTip(tr("Let Codex play this game"));
	m_button->setAccessibleName(tr("Auto Play"));
	window->statusBar()->addPermanentWidget(m_button);
	connect(m_button, &QToolButton::clicked, this, [this](bool checked) { checked ? start() : stop(); });

	connect(&m_process, &QProcess::readyReadStandardOutput, this, &AutoPlay::readUpdates);
	m_process.setStandardErrorFile(QProcess::nullDevice());
	connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		setRunning(false);
		showMessage(tr("Auto Play could not start. Check your Node.js installation."), true);
	});
	connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this]() {
		setRunning(false);
		if (!m_reportedError)
			showMessage(tr("Auto Play is off."));
	});
	connect(window, &Window::gameLoaded, this, [this]() {
		if (m_process.state() == QProcess::NotRunning)
			m_message.clear();
	});
	connect(window, &Window::gameUnloaded, this, &AutoPlay::stop);
	connect(window, &Window::shutdown, this, &AutoPlay::stop);
}

AutoPlay::~AutoPlay() {
	m_process.closeWriteChannel();
	// The runner ends Codex and its MCP server before exiting.
	if (m_process.state() != QProcess::NotRunning && !m_process.waitForFinished(5000)) {
		m_process.kill();
		m_process.waitForFinished(500);
	}
}

void AutoPlay::start() {
	if (m_process.state() != QProcess::NotRunning) {
		setRunning(true);
		return;
	}
	auto fail = [this](const QString& message) {
		setRunning(false);
		showMessage(message, true);
	};
	auto core = m_window->controller();
	if (!core || !core->hasStarted())
		return fail(tr("Auto Play: open a game first."));
	const auto script = runnerScript();
	if (script.isEmpty())
		return fail(tr("Auto Play: open Willow using Launch mGBA.cmd."));
	const auto node = findProgram("node", QDir(qEnvironmentVariable("ProgramFiles")).filePath("nodejs/node.exe"));
	if (node.isEmpty())
		return fail(tr("Auto Play: install Node.js 20 or newer, then reopen Willow."));
	const auto codex = findProgram(
	    "codex", QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath("Programs/OpenAI/Codex/bin/codex.exe"));
	if (codex.isEmpty())
		return fail(tr("Auto Play: install Codex and sign in, then try again."));

	m_stopping = false;
	m_reportedError = false;
	m_updates.clear();
	const auto model = m_window->config()->getOption("autoPlayModel", "terra");
	setRunning(true);
	showMessage(tr("Auto Play is starting…"));
	m_process.start(node, { script, m_sessionId, codex, model });
}

void AutoPlay::stop() {
	if (m_process.state() == QProcess::NotRunning) {
		setRunning(false);
		return;
	}
	if (m_stopping)
		return;
	m_stopping = true;
	showMessage(tr("Auto Play is stopping…"));
	m_process.closeWriteChannel();
}

QJsonObject AutoPlay::status() const {
	const bool running = m_process.state() != QProcess::NotRunning;
	return { { "running", running }, { "stopping", running && m_stopping }, { "message", m_message } };
}

void AutoPlay::setRunning(bool running) {
	m_action->setActive(running);
	m_button->setChecked(running);
	m_button->setText(running ? tr("■ Stop Auto Play") : tr("▶ Auto Play"));
	if (!running)
		m_button->setToolTip(tr("Let Codex play this game"));
}

void AutoPlay::showMessage(const QString& message, bool persistent) {
	m_message = message;
	m_window->statusBar()->showMessage(message, persistent ? 0 : 8000);
	m_button->setToolTip(message);
}

void AutoPlay::readUpdates() {
	m_updates += m_process.readAllStandardOutput();
	if (m_updates.size() > 256 * 1024) {
		m_updates.clear();
		stop();
		return;
	}
	int end;
	while ((end = m_updates.indexOf('\n')) >= 0) {
		const auto update = QJsonDocument::fromJson(m_updates.left(end)).object();
		m_updates.remove(0, end + 1);
		if (m_stopping)
			continue;
		const auto type = update.value("type").toString();
		const auto message = update.value("message").toString();
		if (type == "error") {
			m_reportedError = true;
			showMessage(tr("Auto Play stopped: %1").arg(message), true);
		} else if (type == "status") {
			showMessage(message, update.value("paused").toBool());
		} else if (type == "activity") {
			showMessage(tr("Auto Play: %1").arg(message));
		}
	}
}
