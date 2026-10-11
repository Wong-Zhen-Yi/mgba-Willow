#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QPoint>
#include <QString>
#include <QVector>
#include <functional>

struct mCore;

namespace QGBA {

// Read-only decoder for explicitly verified English Emerald ROM layouts.
// RAM is copied at frame end. All pointers are resolved inside that copy.
class EmeraldGameState {
public:
	static const char* romSha1();
	void identify(mCore* core);
	void capture(mCore* core, quint64 frame, unsigned human, unsigned ai, unsigned effective);
	bool supported() const { return m_supported; }
	bool hasSnapshot() const { return m_wram.size() == 0x40000 && m_iwram.size() == 0x8000; }
	bool ready() const;
	quint64 frame() const { return m_frame; }
	QJsonObject state(bool details = true) const;
	QJsonObject localMap(int radius) const;
	QPoint position() const;
	int mapId() const;
	QString interaction() const;
	bool moving() const;
	bool battleMenuReady() const;
	bool condition(const QString& name) const;
	bool canWalk(QPoint from, QPoint to, bool destination = false) const;
	QVector<QPoint> pathTo(QPoint target, int radius = 16, bool avoidTrainers = true) const;
	bool trainerRisk(QPoint point) const;

	// Also used by fixture tests; production data comes only from backing RAM/ROM.
	void setSnapshot(const QByteArray& rom, const QByteArray& wram, const QByteArray& iwram, quint64 frame);
private:
	friend struct EmeraldGameStateFixture;
	static bool supportsSha1(const QString& sha1);
	bool contains(quint32 address, quint32 length = 1) const;
	quint32 read(quint32 address, int length = 1) const;
	quint32 playerObject() const;
	quint32 layout() const;
	int tile(QPoint point) const;
	int behavior(int metatile) const;
	bool occupied(QPoint point) const;
	QJsonObject tileState(QPoint point) const;
	QJsonObject party() const;
	QJsonObject inventory() const;
	QJsonObject battle() const;
	QJsonObject menu() const;
	QString text(quint32 address, int limit) const;
	void recordEvents();
	QJsonArray m_events;
	QString m_lastMessage;
	bool m_saveConfirmed = false;
	QByteArray m_rom, m_wram, m_iwram;
	QString m_sha1, m_normalizedSha1;
	bool m_supported = false;
	quint64 m_frame = 0;
	unsigned m_human = 0, m_ai = 0, m_effective = 0;
};

// Frame-thread state machine. No timer or IPC round trip participates in movement.
class AIStateAction {
public:
	bool startMove(const EmeraldGameState& state, QPoint target, int maxFrames, bool avoidTrainers = true);
	void startWait(const EmeraldGameState& state, const QString& condition, int maxFrames, int stableFrames);
	bool tick(const EmeraldGameState& state, unsigned human);
	void cancel(const QString& reason, quint64 frame = 0);
	bool pending() const { return m_pending; }
	unsigned keys() const { return m_pending ? m_keys : 0; }
	QJsonObject result() const;
private:
	void stop(const QString& reason, const EmeraldGameState& state);
	bool m_pending = false, m_wait = false;
	bool m_avoidTrainers = true;
	unsigned m_keys = 0;
	int m_remaining = 0, m_stable = 0, m_requiredStable = 1, m_unchanged = 0, m_steps = 0;
	quint64 m_start = 0, m_end = 0;
	int m_map = -1;
	QPoint m_origin, m_last, m_target, m_next;
	QVector<QPoint> m_path;
	QString m_condition, m_reason;
};
}
