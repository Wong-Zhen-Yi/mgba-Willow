#include "AIController.h"
#include "CoreController.h"
#include "Window.h"
#include "VFileDevice.h"
#include "MultiplayerController.h"
#include "ActionMapper.h"

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
#include <QSignalBlocker>
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
	actions->addMenu(tr("AI"), "ai");
	m_toggle = actions->addBooleanAction(tr("AI Control"), "aiControl", [](bool) {}, "ai");
	m_status = new QLabel(tr("AI: off"), window);
	window->statusBar()->addPermanentWidget(m_status);
	connect(m_toggle.get(), &Action::activated, this, [this](bool enabled) {
		if (!enabled) { if (m_core) release(tr("off")); return; }
		auto core = m_window->controller();
		if (!core || !core->hasStarted() || (core->multiplayerController() && core->multiplayerController()->attached() > 1)) { release(tr("open a single-player game")); return; }
		m_core = core;
		m_gamePath = core->path();
		m_coreConnections.append(connect(core.get(), &CoreController::stopping, this, [this]() { release(tr("game stopped")); }));
		m_coreConnections.append(connect(core.get(), &CoreController::didReset, this, [this]() { release(tr("game reset; reconnect")); }));
		m_coreConnections.append(connect(core.get(), &CoreController::stateLoaded, this, [this]() { release(tr("state changed; reconnect")); }));
		m_coreConnections.append(connect(core.get(), &CoreController::crashed, this, [this]() { release(tr("game crashed")); }));
		core->setAIControl(true);
		m_status->setText(tr("AI: waiting for Codex"));
	});
	m_server.setSocketOptions(QLocalServer::UserAccessOption);
	const QString pipe = "mgba-willow-ai-" + m_id;
	if (!m_server.listen(pipe)) {
		m_toggle->setEnabled(false);
		m_status->setText(tr("AI: pipe unavailable"));
		return;
	}
	QDir().mkpath(dataDirectory() + "/sessions");
	m_descriptor = dataDirectory() + "/sessions/" + m_id + ".json";
	QSaveFile descriptor(m_descriptor);
	if (descriptor.open(QIODevice::WriteOnly)) {
		descriptor.write(QJsonDocument(QJsonObject{{"session_id", m_id}, {"pipe", pipe}, {"pid", double(QCoreApplication::applicationPid())}, {"protocol", 1}}).toJson(QJsonDocument::Compact));
		descriptor.commit();
	}
	connect(&m_server, &QLocalServer::newConnection, this, [this]() {
		while (auto socket = m_server.nextPendingConnection()) {
			connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { receive(socket); });
			connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
				if (m_owner == socket) release(tr("disconnected; paused"));
				socket->deleteLater();
			});
		}
	});
	connect(&m_timer, &QTimer::timeout, this, [this]() { poll(); });
	m_timer.start(20);
	connect(window, &Window::shutdown, this, [this]() { release(tr("window closed")); });
}

AIController::~AIController() {
	release(tr("closed"));
	QFile::remove(m_descriptor);
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
		{"connected", !m_owner.isNull()}};
}

QJsonObject AIController::observation() {
	CoreController::Interrupter guard(m_core);
	auto core = m_core->thread()->core;
	const void* pixels = nullptr;
	size_t stride = 0;
	core->getPixels(core, &pixels, &stride);
	unsigned width, height;
	core->currentVideoSize(core, &width, &height);
	if (!pixels) return {{"error", "Framebuffer unavailable"}};
	QImage image(static_cast<const uchar*>(pixels), width, height, stride * BYTES_PER_PIXEL, QImage::Format_RGBX8888);
	QByteArray png;
	QBuffer output(&png);
	output.open(QIODevice::WriteOnly);
	image.save(&output, "PNG");
	auto result = session();
	result.insert("frame", double(m_core->frameCounter()));
	result.insert("paused", m_core->isPaused());
	result.insert("keys", int(core->getKeys(core)));
	result.insert("width", int(width));
	result.insert("height", int(height));
	result.insert("png", QString::fromLatin1(png.toBase64()));
	return result;
}

void AIController::release(const QString& reason) {
	if (!m_pending.isUndefined() && !m_pending.isNull()) reply(m_owner, m_pending, {}, reason);
	m_pending = QJsonValue();
	for (auto connection : m_coreConnections) QObject::disconnect(connection);
	m_coreConnections.clear();
	if (m_core) m_core->setAIControl(false);
	m_core.reset();
	m_owner.clear();
	m_toggle->setActive(false);
	m_status->setText(tr("AI: %1").arg(reason));
}

void AIController::poll() {
	if (m_core && (m_window->controller() != m_core || !m_core->hasStarted() || mCoreThreadHasExited(m_core->thread()) || m_core->path() != m_gamePath)) {
		release(tr("game changed; reconnect"));
		return;
	}
	if (m_pending.isNull() || m_pending.isUndefined()) return;
	if (m_core->isPaused() && m_core->frameCounter() >= m_target) {
		auto id = m_pending;
		m_pending = QJsonValue();
		reply(m_owner, id, observation());
	} else if (QDateTime::currentMSecsSinceEpoch() > m_deadline) release(tr("action timed out; paused"));
}

void AIController::request(QLocalSocket* socket, const QJsonObject& req) {
	const auto id = req.value("id");
	const QString method = req.value("method").toString();
	const auto args = req.value("args").toObject();
	auto fail = [&](const QString& message) { reply(socket, id, {}, message); };
	if (!integer(id, 1, 9007199254740991.0)) { fail(tr("Invalid request id")); return; }
	if (method == "session") { reply(socket, id, session()); return; }
	if (method == "connect") {
		if (!m_pending.isNull() && !m_pending.isUndefined()) { fail(tr("An action is already in progress")); return; }
		if (m_owner && m_owner != socket) { fail(tr("This window is controlled by another client")); return; }
		if (!m_toggle->isActive()) m_toggle->setActive(true);
		if (!m_core) { fail(tr("Open a single-player game first")); return; }
		m_owner = socket;
		m_status->setText(tr("AI: Codex connected"));
		reply(socket, id, observation());
		return;
	}
	if (socket != m_owner || !m_core || !m_core->hasStarted() || m_window->controller() != m_core || m_core->path() != m_gamePath) {
		fail(tr("No controlled game; connect first")); return;
	}
	if (method == "disconnect") { release(tr("disconnected; paused")); reply(socket, id, {{"paused", true}}); return; }
	if (!m_pending.isNull() && !m_pending.isUndefined()) { fail(tr("An action is already in progress")); return; }
	if (!m_core->isPaused()) { fail(tr("Game must be paused")); return; }
	if (method == "observe") { reply(socket, id, observation()); return; }
	if (method == "act") {
		if (!integer(args.value("frames"), 1, 600) || !args.value("buttons").isArray()) { fail(tr("Expected buttons array and 1–600 frames")); return; }
		const QStringList names{"A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L"};
		unsigned keys = 0;
		for (const auto& button : args.value("buttons").toArray()) {
			int index = names.indexOf(button.toString());
			if (!button.isString() || index < 0) { fail(tr("Unknown button")); return; }
			if (m_core->platform() == mPLATFORM_GB && index >= 8) { fail(tr("L/R are only available on GBA")); return; }
			keys |= 1U << index;
		}
		m_target = m_core->frameCounter() + args.value("frames").toInt();
		if (!m_core->advanceAI(keys, args.value("frames").toInt())) { fail(tr("Cannot advance game")); return; }
		m_pending = id;
		m_deadline = QDateTime::currentMSecsSinceEpoch() + 30000;
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
		core->setKeys(core, 0);
		guard.resume();
		if (!save) m_core->refreshAIFrame();
		reply(socket, id, observation());
		return;
	}
	fail(tr("Unknown method"));
}
