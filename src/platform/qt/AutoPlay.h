/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <memory>

class QToolButton;

namespace QGBA {

class Action;
class ActionMapper;
class Window;

// Runs Codex against this window's MCP session (tools/mcp/autoplay.mjs).
class AutoPlay : public QObject {
	Q_OBJECT

public:
	AutoPlay(Window* window, ActionMapper* actions, const QString& sessionId);
	~AutoPlay() override;

	void start(const QString& model = {});
	void stop();
	QJsonObject status() const;
	QJsonArray models() const;
	static bool supportsModel(const QString& model);

private:
	void readUpdates();
	void setRunning(bool running);
	void showMessage(const QString& message, bool persistent = false);

	Window* m_window;
	QString m_sessionId;
	QProcess m_process;
	QByteArray m_updates;
	QString m_message;
	std::shared_ptr<Action> m_action;
	QToolButton* m_button;
	bool m_stopping = false;
	bool m_reportedError = false;
};

}
