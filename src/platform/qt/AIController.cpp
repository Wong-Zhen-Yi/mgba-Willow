#include "AIController.h"
#include "ActionMapper.h"
#include "AutoPlay.h"
#include "ConfigController.h"
#include "CoreController.h"
#include "GameLibrary.h"
#include "willow/MobileVideo.h"
#include "MultiplayerController.h"
#include "Window.h"

#include <QAction>
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QUuid>

using namespace QGBA;
namespace Bridge = Willow::Bridge;

namespace {
QString dataDirectory() {
	return QString::fromStdString(Bridge::dataDirectory());
}
bool integer(const QJsonValue& v, double min, double max) {
	return Bridge::isInteger(toWillowJson(v), min, max);
}
}

AIController::AIController(Window* window, ActionMapper* actions) : QObject(window), m_window(window) {
	m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_descriptor = dataDirectory() + "/sessions/" + m_id + ".json";
	actions->addMenu(tr("AI"), "ai");
	m_enabled = actions->addBooleanAction(tr("MCP Enabled"), "mcpEnabled", [](bool) {}, "ai");
	m_toggle = actions->addBooleanAction(tr("AI Control"), "aiControl", [](bool) {}, "ai");
	actions->addMenu(tr("AI Speed"), "aiSpeed", "ai");
	auto speed = window->config()->addOption("aiSpeed");
	for (int multiplier : {1, 2, 4, 8}) speed->addValue(tr("%1x").arg(multiplier), multiplier, actions, "aiSpeed");
	speed->addValue(tr("Maximum"), -1, actions, "aiSpeed");
	speed->connect([this](const QVariant& value) {
		if (m_core) m_core->setAISpeed(value.toFloat());
		m_window->config()->write();
	}, this);
	int initialSpeed = window->config()->getOption("aiSpeed", 4).toInt();
	if (!Bridge::isAISpeed(initialSpeed)) initialSpeed = 4;
	window->config()->setOption("aiSpeed", initialSpeed);
	m_status = new QLabel(tr("MCP: disabled"), window);
	window->statusBar()->addPermanentWidget(m_status);
	connect(window, &Window::gameLoaded, this, [this]() {
		detachMobile();
		m_gameId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	});
	connect(window, &Window::gameUnloaded, this, [this]() {
		detachMobile();
		m_gameId.clear();
	});
	connect(m_enabled.get(), &Action::activated, this, [this](bool enabled) { if (!m_changingEnabled) setEnabled(enabled); });
	connect(m_toggle.get(), &Action::activated, this, [this](bool enabled) {
		if (!enabled) { if (m_core) release(tr("ready")); return; }
		auto core = m_window->controller();
		if (!m_server.isListening() || !core || !core->hasStarted() || (core->multiplayerController() && core->multiplayerController()->attached() > 1)) {
			release(tr("enable MCP and open a single-player game")); return;
		}
		m_core = core;
		m_gamePath = core->path();
		m_coreConnections.append(connect(core.get(), &CoreController::stopping, this, [this]() { release(tr("ready; game stopped")); }));
		m_coreConnections.append(connect(core.get(), &CoreController::didReset, this, [this]() { release(tr("ready; game reset, reconnect")); }));
		m_coreConnections.append(connect(core.get(), &CoreController::stateLoaded, this, [this]() { release(tr("ready; state changed, reconnect")); }));
		m_coreConnections.append(connect(core.get(), &CoreController::crashed, this, [this]() { release(tr("error: game crashed")); }));
		m_coreConnections.append(connect(core.get(), &CoreController::aiActionFinished, this, [this]() { finishPending(); }));
		m_coreConnections.append(connect(core.get(), &CoreController::paused, this, [this]() { finishPending(true, tr("Game paused; resume manually")); }));
		core->setAISpeed(m_window->config()->getOption("aiSpeed", 4).toFloat());
		core->setAIControl(true);
		m_status->setText(tr("MCP: ready; AI waiting for agents"));
	});
	m_server.setSocketOptions(QLocalServer::UserAccessOption);
	connect(&m_server, &QLocalServer::newConnection, this, [this]() {
		while (auto socket = m_server.nextPendingConnection()) {
			m_sockets.insert(socket);
			connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { receive(socket); });
			connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
				if (m_mobileSocket == socket)
					detachMobile();
				disconnectClient(socket);
				m_sockets.remove(socket);
				socket->deleteLater();
			});
		}
	});
	m_timer.setSingleShot(true);
	connect(&m_timer, &QTimer::timeout, this, [this]() { finishPending(true, tr("Action timed out")); });
	m_mobileLease.setSingleShot(true);
	connect(&m_mobileLease, &QTimer::timeout, this, [this]() { m_mobileCore->releaseMobileKeys(); });
	m_mobileFrameWait.setSingleShot(true);
	connect(&m_mobileFrameWait, &QTimer::timeout, this, &AIController::answerMobileFrame);
	connect(window, &Window::shutdown, this, [this]() { setEnabled(false); });
	m_autoPlay = new AutoPlay(window, actions, m_id);
	m_enabled->setActive(true);
}

AIController::~AIController() {
	setEnabled(false);
	// Member sockets can emit disconnected while the local server is being
	// destroyed, after our tracking set has already been destroyed.
	const auto sockets = m_sockets;
	for (auto socket : sockets) {
		QObject::disconnect(socket, nullptr, this, nullptr);
		socket->abort();
	}
	m_sockets.clear();
}

void AIController::setEnabled(bool enabled) {
	QScopedValueRollback<bool> changing(m_changingEnabled, true);
	if (!enabled) {
		detachMobile();
		release(tr("disabled"));
		m_server.close();
		QFile::remove(m_descriptor);
		const auto sockets = m_sockets;
		for (auto socket : sockets) socket->disconnectFromServer();
		m_toggle->setEnabled(false);
		m_status->setText(tr("MCP: disabled"));
	} else {
		if (m_server.isListening()) return;
		const QString pipe = QString::fromStdString(Bridge::pipeName(m_id.toStdString()));
		bool ok = m_server.listen(pipe) && QDir().mkpath(dataDirectory() + "/sessions");
		if (ok) {
			QSaveFile descriptor(m_descriptor);
			const auto session = Bridge::sessionDescriptor(m_id.toStdString(), QCoreApplication::applicationPid());
			const QByteArray data = QJsonDocument(toQJson(session)).toJson(QJsonDocument::Compact);
			ok = descriptor.open(QIODevice::WriteOnly) && descriptor.write(data) == data.size() && descriptor.commit();
		}
		if (!ok) {
			m_server.close();
			QFile::remove(m_descriptor);
			m_status->setText(tr("MCP: error; cannot create endpoint or discovery file"));
			m_toggle->setEnabled(false);
			m_enabled->setActive(false);
			// Action::trigger finishes its outer activated(true) emission after
			// this callback; synchronize the menu again on the next GUI turn.
			QTimer::singleShot(0, this, [this]() {
				if (m_server.isListening()) return;
				QScopedValueRollback<bool> changing(m_changingEnabled, true);
				m_enabled->setActive(true);
				m_enabled->setActive(false);
			});
			return;
		}
		m_toggle->setEnabled(true);
		m_status->setText(tr("MCP: ready"));
	}
	m_enabled->setActive(enabled);
}

void AIController::reply(QLocalSocket* socket, const QJsonValue& id, const QJsonObject& result, const QString& error,
                         bool showInteraction) {
	if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
	if (showInteraction) {
		const QString outcome = !error.isEmpty() ? tr("error: %1").arg(error) :
		    result.value("cancelled").toBool()   ? tr("cancelled: %1").arg(result.value("reason").toString()) :
		                                           tr("completed");
		m_window->showAIInteraction(tr("MCP #%1  %2").arg(id.toVariant().toString(), outcome));
	}
	QJsonObject response{{"id", id}};
	if (error.isEmpty()) response.insert("result", result);
	else response.insert("error", error);
	socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}

void AIController::detachMobile() {
	m_mobileLease.stop();
	m_mobileFrameWait.stop();
	if (!m_mobileFrameId.isUndefined()) {
		reply(m_mobileSocket, m_mobileFrameId, { }, tr("Game disconnected. Select a game again"), false);
		m_mobileFrameId = QJsonValue(QJsonValue::Undefined);
		m_mobileFrameArgs = { };
	}
	m_mobileVideo.reset();
	m_mobileVideoCodec.clear();
	for (auto connection : m_mobileConnections)
		QObject::disconnect(connection);
	m_mobileConnections.clear();
	if (m_mobileCore) {
		m_mobileCore->releaseMobileKeys();
		m_mobileCore->setMobileSpeed(0);
	}
	m_mobileCore.reset();
	m_mobileSocket.clear();
}

void AIController::mobileRequest(QLocalSocket* socket, const QJsonValue& id, const QString& method,
                                 const QJsonObject& args) {
	auto fail = [&](const QString& message) { reply(socket, id, { }, message, false); };
	if (method == "mobile_session") {
		reply(socket, id, mobileSession(), {}, false);
		return;
	}
	// The phone's first page lists the PC's ROM library and can start a game from it.
	if (method == "mobile_library") {
		auto* library = m_window->gameLibrary();
		QJsonArray games;
		for (const auto& entry : library->entries()) {
			games.append(QJsonObject { { "id", entry.id },
			                           { "title", entry.title },
			                           { "platform", entry.platform },
			                           { "art", entry.artwork } });
		}
		auto core = m_window->controller();
		reply(socket, id,
		      { { "games", games },
		        { "scanning", library->scanning() },
		        { "folder", !library->folder().isEmpty() },
		        { "current_id", core ? GameLibrary::gameId(core->path()) : QString() } },
		      { }, false);
		return;
	}
	if (method == "mobile_artwork") {
		const auto ids = args.value("ids").toArray();
		if (ids.size() > 24) {
			fail(tr("Too many artwork requests"));
			return;
		}
		QJsonObject art;
		for (const auto& value : ids) {
			const auto image = m_window->gameLibrary()->artwork(value.toString(), 192);
			QByteArray jpeg;
			QBuffer output(&jpeg);
			output.open(QIODevice::WriteOnly);
			if (!image.isNull() && image.convertToFormat(QImage::Format_RGB32).save(&output, "JPEG", 82))
				art.insert(value.toString(), QString::fromLatin1(jpeg.toBase64()));
		}
		reply(socket, id, { { "art", art } }, { }, false);
		return;
	}
	if (method == "mobile_launch") {
		const auto game = args.value("id").toString();
		if (auto core = m_window->controller()) {
			if (GameLibrary::gameId(core->path()) == game) {
				reply(socket, id, mobileSession(), {}, false);
				return;
			}
			// Never close a running game from the phone; it may have unsaved progress.
			const auto title = mobileSession().value("title").toString();
			fail(tr("%1 is open on the PC. Save it and return to the game library first.").arg(title));
			return;
		}
		if (!m_window->gameLibrary()->launch(game)) {
			fail(tr("That game is no longer in the library."));
			return;
		}
		reply(socket, id, { { "launching", true } }, { }, false);
		return;
	}
	if (method == "mobile_connect") {
		auto core = m_window->controller();
		if (!core || !core->hasStarted() || m_gameId.isEmpty() || args.value("game_id") != m_gameId ||
		    (core->multiplayerController() && core->multiplayerController()->attached() > 1)) {
			fail(tr("Select a running single-player game"));
			return;
		}
		if (m_mobileSocket && m_mobileSocket != socket) {
			fail(tr("A mobile controller is already connected"));
			return;
		}
		detachMobile();
		m_mobileSocket = socket;
		m_mobileCore = core;
		m_mobileConnections = {
			connect(core.get(), &CoreController::paused, this,
			        [this]() {
			            m_mobileLease.stop();
			            m_mobileCore->releaseMobileKeys();
			            answerMobileFrame();
			        }),
			connect(core.get(), &CoreController::frameAvailable, this, &AIController::answerMobileFrame),
			connect(core.get(), &CoreController::stopping, this, &AIController::detachMobile),
			connect(core.get(), &CoreController::didReset, this, &AIController::detachMobile),
			connect(core.get(), &CoreController::stateLoaded, this, &AIController::detachMobile),
			connect(core.get(), &CoreController::crashed, this, &AIController::detachMobile),
		};
		auto result = mobileSession();
		result.insert("autoplay", m_autoPlay->status());
		result.insert("autoplay_models", m_autoPlay->models());
		result.insert("speed", 0);
		reply(socket, id, result, {}, false);
		return;
	}
	if (!m_mobileCore || m_mobileSocket != socket) {
		fail(tr("Game disconnected. Select a game again"));
		return;
	}
	if (method == "mobile_speed") {
		const int speed = args.value("multiplier").toInt(-2);
		if (!Bridge::isMobileSpeed(speed)) {
			fail(tr("Invalid mobile speed"));
			return;
		}
		m_mobileCore->setMobileSpeed(speed);
		reply(socket, id, { { "speed", speed } }, {}, false);
		return;
	}
	if (method == "mobile_autoplay") {
		const auto enabled = args.value("enabled");
		if (!enabled.isBool()) {
			fail(tr("Invalid Auto Play setting"));
			return;
		}
		const auto model = args.value("model");
		if (!model.isUndefined() && (!enabled.toBool() || !AutoPlay::supportsModel(model.toString()))) {
			fail(tr("Invalid Auto Play model"));
			return;
		}
		if (enabled.toBool())
			m_autoPlay->start(model.toString());
		else
			m_autoPlay->stop();
		reply(socket, id, m_autoPlay->status(), {}, false);
		return;
	}
	if (method == "mobile_keys") {
		if (!integer(args.value("keys"), 0, Bridge::keyMask(m_mobileCore->platform() == mPLATFORM_GBA))) {
			fail(tr("Invalid mobile buttons"));
			return;
		}
		if (!m_mobileCore->isPaused()) {
			m_mobileCore->setMobileKeys(args.value("keys").toInt());
			m_mobileLease.start(500);
		}
		reply(socket, id, { }, { }, false);
		return;
	}
	if (method == "mobile_frame") {
		if (!m_mobileFrameId.isUndefined()) {
			fail(tr("A frame request is already waiting"));
			return;
		}
		mobileFrame(id, args, false);
		return;
	}
	fail(tr("Unknown mobile command"));
}

// Sends the newest frame as PNG, H.264, or HEVC.
// With args.wait, a video request for a frame the phone already has is held
// until the core finishes the next one, so the stream follows the game's own
// frame rate rather than a polling timer.
void AIController::mobileFrame(const QJsonValue& id, const QJsonObject& args, bool waited) {
	CoreController::Interrupter guard(m_mobileCore);
	const auto frame = double(m_mobileCore->frameCounter());
	const bool paused = m_mobileCore->isPaused();
	const auto codec = args.value("codec").toString();
	const bool video = codec == "hevc" || codec == "h264";
	if (codec != m_mobileVideoCodec) {
		m_mobileVideo.reset();
		m_mobileVideoCodec = codec;
	}
	QJsonObject result { { "paused", paused }, { "frame", frame }, { "autoplay", m_autoPlay->status() } };
	if (args.value("after") == QJsonValue(frame)) {
		guard.resume();
		if (video && args.value("wait").toBool() && !waited && !paused) {
			m_mobileFrameId = id;
			m_mobileFrameArgs = args;
			m_mobileFrameWait.start(250);
			return;
		}
		reply(m_mobileSocket, id, result, { }, false);
		return;
	}
	const auto image = m_mobileCore->getPixels().copy();
	guard.resume();
	if (video) {
		if (!m_mobileVideo)
			m_mobileVideo = std::make_unique<MobileVideoEncoder>(MobileVideoEncoder::defaultEncoders(codec == "hevc"));
		const QImage rgbx = image.format() == QImage::Format_RGBX8888 ? image : image.convertToFormat(QImage::Format_RGBX8888);
		const Willow::VideoFrame frame { rgbx.constBits(), rgbx.width(), rgbx.height(), int(rgbx.bytesPerLine()) };
		std::vector<MobileVideoEncoder::Packet> packets;
		if (m_mobileVideo->encode(frame, args.value("keyframe").toBool(), &packets)) {
			QJsonArray encoded;
			for (const auto& packet : packets) {
				const auto data = QByteArray::fromRawData(reinterpret_cast<const char*>(packet.data.data()), int(packet.data.size()));
				encoded.append(QJsonObject { { "key", packet.key }, { "data", QString::fromLatin1(data.toBase64()) } });
			}
			result.insert("video", QJsonObject { { "encoder", QString::fromStdString(m_mobileVideo->encoder()) },
			                                     { "hardware", m_mobileVideo->hardware() },
			                                     { "codec", QString::fromStdString(m_mobileVideo->codec()) },
			                                     { "width", m_mobileVideo->size().width },
			                                     { "height", m_mobileVideo->size().height },
			                                     { "packets", encoded } });
		} else {
			result.insert("video_error", QString::fromStdString(m_mobileVideo->error()));
		}
	} else {
		QByteArray png;
		QBuffer output(&png);
		output.open(QIODevice::WriteOnly);
		if (image.save(&output, "PNG"))
			result.insert("png", QString::fromLatin1(png.toBase64()));
	}
	reply(m_mobileSocket, id, result, { }, false);
}

void AIController::answerMobileFrame() {
	if (m_mobileFrameId.isUndefined()) return;
	m_mobileFrameWait.stop();
	const auto id = m_mobileFrameId;
	const auto args = m_mobileFrameArgs;
	m_mobileFrameId = QJsonValue(QJsonValue::Undefined);
	m_mobileFrameArgs = { };
	if (m_mobileCore) mobileFrame(id, args, true);
}

void AIController::receive(QLocalSocket* socket) {
	// Bound messages before allocating or parsing them.
	if (socket->bytesAvailable() > qint64(Bridge::MaxLineBytes)) { socket->abort(); return; }
	while (socket->canReadLine()) {
		QJsonParseError error;
		auto document = QJsonDocument::fromJson(socket->readLine(Bridge::MaxLineBytes + 1), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject()) {
			reply(socket, {}, {}, tr("Invalid JSON request"));
			continue;
		}
		request(socket, document.object());
	}
}

Bridge::Session AIController::sessionInfo() const {
	Bridge::Session session;
	session.id = m_id.toStdString();
	session.clients = int(m_clients.size());
	session.mcpEnabled = m_server.isListening();
	session.objective = m_window->aiObjective().toStdString();
	session.aiSpeed = m_window->config()->getOption("aiSpeed", 4).toInt();
	if (auto core = m_window->controller()) {
		session.hasGame = true;
		session.title = core->title().toStdString();
		session.gameId = m_gameId.toStdString();
		session.gba = core->platform() == mPLATFORM_GBA;
		session.started = core->hasStarted();
		session.aiControl = core->aiControl();
		session.frame = core->frameCounter();
		session.paused = core->isPaused();
		session.openGL = core->hardwareAccelerated();
		session.humanKeys = core->humanKeys();
		session.aiKeys = core->aiKeys();
	}
	return session;
}

QJsonObject AIController::session() const {
	return toQJson(Bridge::sessionJson(sessionInfo()));
}

QJsonObject AIController::mobileSession() const {
	return toQJson(Bridge::mobileSessionJson(sessionInfo()));
}

QJsonObject AIController::observation(bool screenshot) {
	CoreController::Interrupter guard(m_core);
	auto core = m_core->thread()->core;
	// Use the completed frame, rather than a framebuffer halfway through
	// rendering. Detach the image before letting emulation continue.
	QImage image;
	if (screenshot) image = m_core->getPixels().copy();
	if (screenshot && image.isNull()) return {{"error", "Framebuffer unavailable"}};
	auto result = session();
	result.insert("frame", double(m_core->frameCounter()));
	result.insert("paused", m_core->isPaused());
	result.insert("keys", int(core->getKeys(core)));
	result.insert("effective_keys", int(core->getKeys(core)));
	result.insert("game_state", toQJson(m_core->aiGameState().state()));
	result.insert("atomic", m_core->aiAtomicFrame());
	guard.resume();
	if (!screenshot) return result;
	QByteArray png;
	QBuffer output(&png);
	output.open(QIODevice::WriteOnly);
	image.save(&output, "PNG");
	result.insert("width", image.width());
	result.insert("height", image.height());
	result.insert("png", QString::fromLatin1(png.toBase64()));
	return result;
}

QJsonArray AIController::completedSteps() const {
	if (!m_core) return { };
	return toQJsonValue(Bridge::completedSteps(m_start, m_stepFrames, m_core->aiCompletedSteps())).toArray();
}

void AIController::finishPending(bool cancelled, const QString& reason) {
	if (!m_core || m_pending.isUndefined() || m_pending.isNull()) return;
	CoreController::Interrupter guard(m_core);
	if (m_window->controller() != m_core || m_core->path() != m_gamePath) cancelled = true;
	if (!cancelled && (m_core->aiActionPending() || m_core->frameCounter() < m_core->aiActionEnd())) return;
	// A pause arriving after the final frame still returns successful completion.
	cancelled = cancelled && (m_stateAction ? m_core->aiActionPending() : m_core->aiCompletedSteps() < size_t(m_stepFrames.size()));
	auto stateActionResult = m_core->aiStateActionResult();
	if (cancelled) m_core->cancelAIAction();
	auto result = observation(m_screenshot && !cancelled);
	result.insert("cancelled", cancelled);
	result.insert("action_start_frame", double(m_start));
	if (!cancelled) result.insert("action_end_frame", double(m_core->aiActionEnd()));
	else result.insert("reason", reason);
	if (m_sequence) result.insert("steps", completedSteps());
	if (m_stateAction) {
		if (cancelled) { stateActionResult.insert("reason", reason); stateActionResult.insert("completed", false); stateActionResult.insert("stop_frame", double(m_core->frameCounter())); }
		result.insert("progress", stateActionResult);
	}
	else if (m_directionKeys) {
		const auto progress = m_movement.progress(uintptr_t(m_actionOwner.data()), m_actionState,
		                                          m_core->aiGameState().state(false), m_directionKeys);
		if (!progress.is_null()) result.insert("progress", toQJson(progress));
	}
	const auto id = m_pending;
	const auto owner = m_actionOwner;
	m_pending = QJsonValue();
	m_actionOwner.clear();
	m_timer.stop();
	reply(owner, id, result);
}

void AIController::disconnectClient(QLocalSocket* socket) {
	if (!m_clients.remove(socket)) return;
	if (m_actionOwner == socket) finishPending(true, tr("Action client disconnected"));
	m_movement.forget(uintptr_t(socket));
	if (m_clients.isEmpty()) release(tr("ready; disconnected"));
	else m_status->setText(tr("MCP: connected; %1 AI clients").arg(m_clients.size()));
}

void AIController::release(const QString& reason) {
	finishPending(true, reason);
	m_timer.stop();
	for (auto connection : m_coreConnections) QObject::disconnect(connection);
	m_coreConnections.clear();
	if (m_core) m_core->setAIControl(false);
	m_core.reset();
	m_clients.clear();
	m_movement.clear();
	m_actionOwner.clear();
	m_toggle->setActive(false);
	m_status->setText(tr("MCP: %1").arg(reason));
}

void AIController::request(QLocalSocket* socket, const QJsonObject& req) {
	const auto id = req.value("id");
	const QString method = req.value("method").toString();
	const auto args = req.value("args").toObject();
	auto fail = [&](const QString& message) { reply(socket, id, {}, message); };
	auto failWith = [&](const std::string& message) { fail(QString::fromStdString(message)); };
	if (!integer(id, 1, 9007199254740991.0)) { fail(tr("Invalid request id")); return; }
	if (method.startsWith("mobile_")) {
		if (m_server.isListening())
			mobileRequest(socket, id, method, args);
		else
			reply(socket, id, { }, tr("MCP is disabled"), false);
		return;
	}
	m_window->showAIInteraction(tr("MCP #%1  %2 %3").arg(id.toVariant().toString(), method.left(128),
		QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))));
	if (!m_server.isListening()) { fail(tr("MCP is disabled")); return; }
	if (method == "session") { reply(socket, id, session()); return; }
	if (method == "connect") {
		if (!m_toggle->isActive()) m_toggle->setActive(true);
		if (!m_core) { fail(tr("Open a single-player game first")); return; }
		m_clients.insert(socket);
		m_status->setText(tr("MCP: connected; %1 AI clients").arg(m_clients.size()));
		reply(socket, id, observation());
		return;
	}
	if (!m_clients.contains(socket) || !m_core || !m_core->hasStarted() || m_window->controller() != m_core || m_core->path() != m_gamePath) {
		fail(tr("No controlled game; connect first")); return;
	}
	const std::string name = method.toStdString();
	const Willow::Json arguments = toWillowJson(args);
	std::string error;
	if (method == "disconnect") {
		const bool paused = m_core->isPaused();
		disconnectClient(socket); reply(socket, id, {{"paused", paused}}); return;
	}
	if (method == "set_objective") {
		if (!Bridge::isObjective(arguments.value("text", Willow::Json()))) {
			fail(tr("Expected objective text of at most 512 characters"));
			return;
		}
		m_window->setAIObjective(args.value("text").toString());
		reply(socket, id, { { "objective", m_window->aiObjective() } });
		return;
	}
	if (method == "observe" || method == "get_game_state" || method == "get_local_map") {
		if (!Bridge::checkObserve(name, arguments, &error)) { failWith(error); return; }
		CoreController::Interrupter guard(m_core);
		auto result = observation(args.value("screenshot").toBool(method == "observe"));
		if (method == "get_local_map") result.insert("local_map", toQJson(m_core->aiGameState().localMap(args.value("radius").toInt(4))));
		reply(socket, id, result); return;
	}
	if (!m_pending.isNull() && !m_pending.isUndefined() && method != "memory_map" && method != "read_memory") {
		fail(tr("An action is already in progress; observe and retry after it completes")); return;
	}
	if (method == "set_speed") {
		int speed = 0;
		if (!Bridge::parseSpeed(arguments.value("multiplier", Willow::Json()), &speed)) {
			fail(tr("Expected speed 1, 2, 4, 8, or maximum")); return;
		}
		m_window->config()->setOption("aiSpeed", speed);
		m_window->config()->write();
		reply(socket, id, session()); return;
	}
	if (method == "move_to" || method == "wait_until") {
		if (!Bridge::checkStateAction(name, arguments, &error)) { failWith(error); return; }
		CoreController::Interrupter guard(m_core);
		if (!m_core->aiGameState().supported()) { fail(tr("Unsupported ROM; use the raw tools")); return; }
		if (!m_core->startAIStateAction(method, args)) { fail(tr("Cannot start: resume manually, release human input, and wait for an initialized overworld on foot")); return; }
		m_stateAction = true; m_sequence = false; m_stepFrames.clear();
		m_start = m_core->frameCounter(); m_screenshot = args.value("screenshot").toBool(false);
		m_pending = id; m_actionOwner = socket; m_timer.start(Bridge::ActionTimeoutMs);
		if (!m_core->aiActionPending()) finishPending();
		return;
	}
	if (method == "act" || method == "act_sequence" || method == "press") {
		Bridge::ActionPlan plan;
		if (!Bridge::parseAction(name, arguments, m_core->platform() != mPLATFORM_GB, m_core->isPaused(), &plan, &error)) {
			failWith(error);
			return;
		}
		CoreController::Interrupter guard(m_core);
		m_actionState = m_core->aiGameState().state(false);
		m_directionKeys = plan.directionKeys;
		if (!m_core->advanceAI(plan.steps)) { fail(tr("Cannot advance game; resume manually if paused")); return; }
		m_start = m_core->aiActionStart();
		m_stateAction = false;
		m_stepFrames = plan.frames;
		m_sequence = plan.sequence;
		m_screenshot = plan.screenshot;
		m_pending = id;
		m_actionOwner = socket;
		m_timer.start(Bridge::ActionTimeoutMs);
		return;
	}
	CoreController::Interrupter guard(m_core);
	auto core = m_core->thread()->core;
	if (method == "memory_map") {
		reply(socket, id, toQJson(Bridge::memoryMap(core)));
		return;
	}
	if (method == "read_memory") {
		Willow::Json result;
		if (!Bridge::readMemory(core, arguments, &result, &error)) { failWith(error); return; }
		reply(socket, id, toQJson(result));
		return;
	}
	if (method == "save_checkpoint" || method == "load_checkpoint") {
		if (!Bridge::checkpoint(core, name, arguments, &error)) { failWith(error); return; }
		if (method == "load_checkpoint") {
			m_core->cancelAIAction();
			m_core->refreshAIFrame();
			m_movement.clear();
		}
		guard.resume();
		reply(socket, id, observation());
		return;
	}
	fail(tr("Unknown method"));
}
