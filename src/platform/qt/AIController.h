#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <memory>
#include <vector>

#include "willow/Bridge.h"

class QLabel;
class QLocalSocket;

namespace QGBA {
class Window;
class CoreController;
class Action;
class ActionMapper;
class AutoPlay;
}

namespace Willow {
class MobileVideoEncoder;
}

namespace QGBA {
using Willow::MobileVideoEncoder;

// GUI-thread IPC endpoint; emulation access is synchronized by CoreController.
class AIController : public QObject {
public:
	AIController(Window* window, ActionMapper* actions);
	~AIController();

private:
	void receive(QLocalSocket* socket);
	void request(QLocalSocket* socket, const QJsonObject& request);
	void reply(QLocalSocket* socket, const QJsonValue& id, const QJsonObject& result, const QString& error = { },
	           bool showInteraction = true);
	void mobileRequest(QLocalSocket* socket, const QJsonValue& id, const QString& method, const QJsonObject& args);
	void mobileFrame(const QJsonValue& id, const QJsonObject& args, bool waited);
	void answerMobileFrame();
	void detachMobile();
	void attach();
	void release(const QString& reason);
	void disconnectClient(QLocalSocket* socket);
	void setEnabled(bool enabled);
	void finishPending(bool cancelled = false, const QString& reason = { });
	QJsonArray completedSteps() const;
	QJsonObject observation(bool screenshot = true);
	Willow::Bridge::Session sessionInfo() const;
	QJsonObject session() const;
	QJsonObject mobileSession() const;
	Window* m_window;
	AutoPlay* m_autoPlay;
	QLocalServer m_server;
	QSet<QLocalSocket*> m_clients;
	QPointer<QLocalSocket> m_actionOwner;
	std::shared_ptr<CoreController> m_core;
	std::shared_ptr<Action> m_enabled;
	QSet<QLocalSocket*> m_sockets;
	QLabel* m_status;
	QTimer m_timer;
	QTimer m_mobileLease;
	QPointer<QLocalSocket> m_mobileSocket;
	std::shared_ptr<CoreController> m_mobileCore;
	QList<QMetaObject::Connection> m_mobileConnections;
	std::unique_ptr<MobileVideoEncoder> m_mobileVideo;
	QString m_mobileVideoCodec;
	QTimer m_mobileFrameWait;
	QJsonValue m_mobileFrameId = QJsonValue(QJsonValue::Undefined);
	QJsonObject m_mobileFrameArgs;
	QString m_id;
	QString m_gameId;
	QString m_descriptor;
	QString m_gamePath;
	QJsonValue m_pending;
	quint64 m_start = 0;
	std::vector<int> m_stepFrames;
	bool m_screenshot = true;
	bool m_sequence = false;
	bool m_stateAction = false;
	Willow::Json m_actionState;
	unsigned m_directionKeys = 0;
	Willow::Bridge::MovementTracker m_movement;
	bool m_changingEnabled = false;
	QList<QMetaObject::Connection> m_coreConnections;
};
}
