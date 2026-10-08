#include "AIController.h"
#include "CoreController.h"
#include "Window.h"
#include "VFileDevice.h"
#include "MultiplayerController.h"
#include "ActionMapper.h"
#include "ConfigController.h"

#include <QAction>
#include <QBuffer>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocalSocket>
#include <QMenuBar>
#include <QMenu>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUuid>
#include <QRegularExpression>
#include <cmath>
#include <mgba/core/serialize.h>

using namespace QGBA;

namespace {
QString dataDirectory() {
	QString root = qEnvironmentVariable("LOCALAPPDATA");
	if (root.isEmpty()) root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	return root + "/mgba-willow/ai";
}
bool integer(const QJsonValue& v, double min, double max) {
	return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() == std::floor(v.toDouble()) && v.toDouble() >= min && v.toDouble() <= max;
}
bool readable(const mCoreMemoryBlock& b) {
	// Only ordinary ROM/RAM backing stores: no MMIO, BIOS, virtual registers,
	// cartridge control, or EEPROM interfaces.
	const QString name = QString::fromUtf8(b.internalName);
	const QStringList safe{"cart0", "cart1", "cart2", "wram", "iwram", "vram", "palette", "oam", "hram"};
	return (b.flags & mCORE_MEMORY_READ) && !(b.flags & mCORE_MEMORY_VIRTUAL) && safe.contains(name);
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
	if (initialSpeed != 1 && initialSpeed != 2 && initialSpeed != 4 && initialSpeed != 8 && initialSpeed != -1) initialSpeed = 4;
	window->config()->setOption("aiSpeed", initialSpeed);
	m_status = new QLabel(tr("MCP: disabled"), window);
	window->statusBar()->addPermanentWidget(m_status);
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
		m_status->setText(tr("MCP: ready; AI waiting for Codex"));
	});
	m_server.setSocketOptions(QLocalServer::UserAccessOption);
	connect(&m_server, &QLocalServer::newConnection, this, [this]() {
		while (auto socket = m_server.nextPendingConnection()) {
			m_sockets.insert(socket);
			connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { receive(socket); });
			connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
				if (m_owner == socket) release(tr("ready; disconnected"));
				m_sockets.remove(socket);
				socket->deleteLater();
			});
		}
	});
	m_timer.setSingleShot(true);
	connect(&m_timer, &QTimer::timeout, this, [this]() { release(tr("ready; action timed out")); });
	connect(window, &Window::shutdown, this, [this]() { setEnabled(false); });
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
		release(tr("disabled"));
		m_server.close();
		QFile::remove(m_descriptor);
		const auto sockets = m_sockets;
		for (auto socket : sockets) socket->disconnectFromServer();
		m_toggle->setEnabled(false);
		m_status->setText(tr("MCP: disabled"));
	} else {
		if (m_server.isListening()) return;
		const QString pipe = "mgba-willow-ai-" + m_id;
		bool ok = m_server.listen(pipe) && QDir().mkpath(dataDirectory() + "/sessions");
		if (ok) {
			QSaveFile descriptor(m_descriptor);
			const QByteArray data = QJsonDocument(QJsonObject{{"session_id", m_id}, {"pipe", pipe}, {"pid", double(QCoreApplication::applicationPid())}, {"protocol", 1}}).toJson(QJsonDocument::Compact);
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

void AIController::reply(QLocalSocket* socket, const QJsonValue& id, const QJsonObject& result, const QString& error) {
	if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
	QJsonObject response{{"id", id}};
	if (error.isEmpty()) response.insert("result", result);
	else response.insert("error", error);
	socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}

void AIController::receive(QLocalSocket* socket) {
	// Bound messages before allocating or parsing them.
	if (socket->bytesAvailable() > 65536) { socket->abort(); return; }
	while (socket->canReadLine()) {
		QJsonParseError error;
		auto document = QJsonDocument::fromJson(socket->readLine(65537), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject()) {
			reply(socket, {}, {}, tr("Invalid JSON request"));
			continue;
		}
		request(socket, document.object());
	}
}

QJsonObject AIController::session() const {
	auto core = m_window->controller();
	return {{"session_id", m_id}, {"title", core ? core->title() : QString()},
		{"platform", core ? (core->platform() == mPLATFORM_GBA ? "GBA" : "GB/GBC") : "none"},
		{"started", core && core->hasStarted()}, {"ai_control", core && core->aiControl()},
		{"frame", core ? double(core->frameCounter()) : 0}, {"paused", core && core->isPaused()},
		{"connected", !m_owner.isNull()}, {"mcp_enabled", m_server.isListening()},
		{"ai_speed", m_window->config()->getOption("aiSpeed", 4).toInt() == -1 ? QJsonValue("maximum") : QJsonValue(m_window->config()->getOption("aiSpeed", 4).toInt())},
		{"human_keys", core ? int(core->humanKeys()) : 0}, {"ai_keys", core ? int(core->aiKeys()) : 0},
		{"effective_keys", core ? int(AIGameplay::mergeKeys(core->humanKeys(), core->aiKeys())) : 0}};
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
	QJsonArray steps;
	if (!m_core) return steps;
	quint64 frame = m_start;
	for (size_t i = 0; i < m_core->aiCompletedSteps() && i < size_t(m_stepFrames.size()); ++i) {
		steps.append(QJsonObject{{"action_start_frame", double(frame)}, {"action_end_frame", double(frame + m_stepFrames[int(i)])}});
		frame += m_stepFrames[int(i)];
	}
	return steps;
}

void AIController::finishPending(bool cancelled, const QString& reason) {
	if (!m_core || m_pending.isUndefined() || m_pending.isNull()) return;
	CoreController::Interrupter guard(m_core);
	if (m_window->controller() != m_core || m_core->path() != m_gamePath) cancelled = true;
	if (!cancelled && (m_core->aiActionPending() || m_core->frameCounter() < m_core->aiActionEnd())) return;
	// A pause arriving after the final frame still returns successful completion.
	cancelled = cancelled && m_core->aiCompletedSteps() < size_t(m_stepFrames.size());
	if (cancelled) m_core->cancelAIAction();
	auto result = observation(m_screenshot && !cancelled);
	result.insert("cancelled", cancelled);
	result.insert("action_start_frame", double(m_start));
	if (!cancelled) result.insert("action_end_frame", double(m_core->aiActionEnd()));
	else result.insert("reason", reason);
	if (m_sequence) result.insert("steps", completedSteps());
	const auto id = m_pending;
	m_pending = QJsonValue();
	m_timer.stop();
	reply(m_owner, id, result);
}

void AIController::release(const QString& reason) {
	finishPending(true, reason);
	m_timer.stop();
	for (auto connection : m_coreConnections) QObject::disconnect(connection);
	m_coreConnections.clear();
	if (m_core) m_core->setAIControl(false);
	m_core.reset();
	m_owner.clear();
	m_toggle->setActive(false);
	m_status->setText(tr("MCP: %1").arg(reason));
}

void AIController::request(QLocalSocket* socket, const QJsonObject& req) {
	const auto id = req.value("id");
	const QString method = req.value("method").toString();
	const auto args = req.value("args").toObject();
	auto fail = [&](const QString& message) { reply(socket, id, {}, message); };
	if (!integer(id, 1, 9007199254740991.0)) { fail(tr("Invalid request id")); return; }
	if (!m_server.isListening()) { fail(tr("MCP is disabled")); return; }
	if (method == "session") { reply(socket, id, session()); return; }
	if (method == "connect") {
		if (!m_pending.isNull() && !m_pending.isUndefined()) { fail(tr("An action is already in progress")); return; }
		if (m_owner && m_owner != socket) { fail(tr("This window is controlled by another client")); return; }
		if (!m_toggle->isActive()) m_toggle->setActive(true);
		if (!m_core) { fail(tr("Open a single-player game first")); return; }
		m_owner = socket;
		m_status->setText(tr("MCP: connected; shared AI control"));
		reply(socket, id, observation());
		return;
	}
	if (socket != m_owner || !m_core || !m_core->hasStarted() || m_window->controller() != m_core || m_core->path() != m_gamePath) {
		fail(tr("No controlled game; connect first")); return;
	}
	if (method == "disconnect") {
		const bool paused = m_core->isPaused();
		release(tr("ready; disconnected")); reply(socket, id, {{"paused", paused}}); return;
	}
	if (!m_pending.isNull() && !m_pending.isUndefined()) { fail(tr("An action is already in progress")); return; }
	if (method == "observe") { reply(socket, id, observation()); return; }
	if (method == "set_speed") {
		const auto value = args.value("multiplier");
		int speed = value == QJsonValue("maximum") ? -1 : value.toInt(0);
		if (value != QJsonValue("maximum") && (!integer(value, 1, 8) || (speed != 1 && speed != 2 && speed != 4 && speed != 8))) {
			fail(tr("Expected speed 1, 2, 4, 8, or maximum")); return;
		}
		m_window->config()->setOption("aiSpeed", speed);
		m_window->config()->write();
		reply(socket, id, session()); return;
	}
	if (method == "act" || method == "act_sequence") {
		if (m_core->isPaused()) { fail(tr("Game paused; resume manually before acting")); return; }
		if (args.contains("screenshot") && !args.value("screenshot").isBool()) { fail(tr("Expected boolean screenshot")); return; }
		QJsonArray actions;
		if (method == "act") actions.append(args);
		else if (args.value("actions").isArray()) actions = args.value("actions").toArray();
		if (actions.isEmpty() || actions.size() > 32) { fail(tr("Expected 1–32 actions")); return; }
		const QStringList names{"A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L"};
		std::vector<AIGameplay::Step> steps;
		QList<int> durations;
		int total = 0;
		for (const auto& value : actions) {
			const auto action = value.toObject();
			if (!value.isObject() || !integer(action.value("frames"), 1, 600) || !action.value("buttons").isArray() || action.value("buttons").toArray().size() > 10) {
				fail(tr("Expected buttons array and 1–600 frames")); return;
			}
			unsigned keys = 0;
			for (const auto& button : action.value("buttons").toArray()) {
				int index = names.indexOf(button.toString());
				if (!button.isString() || index < 0) { fail(tr("Unknown button")); return; }
				if (m_core->platform() == mPLATFORM_GB && index >= 8) { fail(tr("L/R are only available on GBA")); return; }
				keys |= 1U << index;
			}
			int frames = action.value("frames").toInt();
			total += frames;
			durations.append(frames);
			steps.push_back({keys, frames});
		}
		if (total > 600) { fail(tr("Sequence exceeds 600 total frames")); return; }
		CoreController::Interrupter guard(m_core);
		if (!m_core->advanceAI(steps)) { fail(tr("Cannot advance game; resume manually if paused")); return; }
		m_start = m_core->aiActionStart();
		m_stepFrames = durations;
		m_sequence = method == "act_sequence";
		m_screenshot = args.value("screenshot").toBool(true);
		m_pending = id;
		m_timer.start(30000);
		return;
	}
	CoreController::Interrupter guard(m_core);
	auto core = m_core->thread()->core;
	if (method == "memory_map" || method == "read_memory") {
		const mCoreMemoryBlock* blocks = nullptr;
		size_t count = core->listMemoryBlocks(core, &blocks);
		if (method == "memory_map") {
			QJsonArray regions;
			for (size_t i = 0; i < count; ++i) {
				const auto& b = blocks[i];
				regions.append(QJsonObject{{"name", QString::fromUtf8(b.internalName)}, {"description", QString::fromUtf8(b.longName)},
					{"start", double(b.start)}, {"end", double(b.end)}, {"size", double(b.size)},
					{"max_segment", b.maxSegment}, {"segment_start", double(b.segmentStart)}, {"readable", readable(b)}});
			}
			reply(socket, id, {{"regions", regions}}); return;
		}
		if (!integer(args.value("address"), 0, 4294967295.0) || !integer(args.value("length"), 1, 4096) ||
			(args.contains("segment") && !integer(args.value("segment"), -1, 65535))) { fail(tr("Invalid address, length, or segment")); return; }
		const auto address = quint32(args.value("address").toDouble());
		const int length = args.value("length").toInt();
		const int segment = args.value("segment").toInt(-1);
		const mCoreMemoryBlock* region = nullptr;
		for (size_t i = 0; i < count; ++i) if (readable(blocks[i]) && address >= blocks[i].start && quint64(address) + length <= blocks[i].end) { region = &blocks[i]; break; }
		if (!region || !readable(*region) || !region->size || (segment >= 0 && (segment > region->maxSegment || address < region->segmentStart))) {
			fail(tr("Unsupported memory region, range, or bank; consult memory_map")); return;
		}
		QByteArray bytes(length, '\0');
		if (core->platform(core) == mPLATFORM_GBA) {
			// GBA rawRead8 can enter GPIO/EEPROM handlers in cartridge space.
			// Read the backing store directly to guarantee no peripheral effects.
			size_t size = 0;
			const auto* backing = static_cast<const char*>(core->getMemoryBlock(core, region->id, &size));
			const quint64 offset = address - region->start;
			if (!backing || offset + length > size) { fail(tr("Read exceeds memory backing store")); return; }
			bytes = QByteArray(backing + offset, length);
		} else {
			for (int i = 0; i < length; ++i) bytes[i] = core->rawRead8(core, address + i, segment);
		}
		reply(socket, id, {{"address", double(address)}, {"length", length}, {"segment", segment}, {"hex", QString::fromLatin1(bytes.toHex())}});
		return;
	}
	if (method == "save_checkpoint" || method == "load_checkpoint") {
		QString name = args.value("name").toString();
		if (!QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(name).hasMatch()) { fail(tr("Checkpoint names must contain 1–64 letters, digits, underscores, or hyphens")); return; }
		quint32 crc = 0;
		core->checksum(core, &crc, mCHECKSUM_CRC32);
		QString directory = dataDirectory() + "/checkpoints/" + QString::number(crc, 16);
		if (!QDir().mkpath(directory)) { fail(tr("Cannot create checkpoint directory")); return; }
		const QString path = directory + "/" + name + ".state";
		bool save = method == "save_checkpoint";
		bool ok = false;
		if (save) {
			VFileDevice state(VFileDevice::openMemory());
			if (state.isOpen() && mCoreSaveStateNamed(core, state, SAVESTATE_ALL) && state.seek(0)) {
				const QByteArray bytes = state.readAll();
				QSaveFile output(path);
				ok = output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
			}
		} else {
			VFileDevice file(path, QIODevice::ReadOnly);
			if (!file.isOpen()) { fail(tr("Cannot open checkpoint")); return; }
			ok = mCoreLoadStateNamed(core, file, SAVESTATE_ALL);
		}
		if (!ok) { fail(tr("Checkpoint operation failed (wrong ROM or damaged state)")); return; }
		if (!save) m_core->cancelAIAction();
		if (!save) m_core->refreshAIFrame();
		guard.resume();
		reply(socket, id, observation());
		return;
	}
	fail(tr("Unknown method"));
}
