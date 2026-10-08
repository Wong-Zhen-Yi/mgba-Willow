#pragma once

#include <QObject>
#include <QJsonObject>
#include <QLocalServer>
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <QJsonArray>
#include <memory>

class QLabel;
class QLocalSocket;

namespace QGBA {
class Window;
class CoreController;
class Action;
class ActionMapper;

// GUI-thread IPC endpoint; emulation access is synchronized by CoreController.
class AIController : public QObject {
public:
	AIController(Window* window, ActionMapper* actions);
	~AIController();
private:
	void receive(QLocalSocket* socket);
	void request(QLocalSocket* socket, const QJsonObject& request);
	void reply(QLocalSocket* socket, const QJsonValue& id, const QJsonObject& result, const QString& error = {});
	void release(const QString& reason);
	void setEnabled(bool enabled);
	void finishPending(bool cancelled = false, const QString& reason = {});
	QJsonArray completedSteps() const;
	QJsonObject observation(bool screenshot = true);
	QJsonObject session() const;
	Window* m_window;
	QLocalServer m_server;
	QPointer<QLocalSocket> m_owner;
	std::shared_ptr<CoreController> m_core;
	std::shared_ptr<Action> m_toggle;
	std::shared_ptr<Action> m_enabled;
	QSet<QLocalSocket*> m_sockets;
	QLabel* m_status;
	QTimer m_timer;
	QString m_id;
	QString m_descriptor;
	QString m_gamePath;
	QJsonValue m_pending;
	quint64 m_start = 0;
	QList<int> m_stepFrames;
	bool m_screenshot = true;
	bool m_sequence = false;
	bool m_changingEnabled = false;
	QList<QMetaObject::Connection> m_coreConnections;
};
}
