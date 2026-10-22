/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "LocalModelGoalDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>

using namespace QGBA;

LocalModelGoalDialog::LocalModelGoalDialog(const QVector<LocalModelGoal>& goals, const QString& previousGoal,
                                           QWidget* parent)
    : QDialog(parent) {
	setObjectName("localModelGoalDialog");
	setWindowTitle(tr("Local gameplay goal"));
	resize(600, 400);
	setStyleSheet(QStringLiteral(
	    "QDialog { background: #111827; }"
	    "QLabel, QCheckBox { color: #edf2fb; }"
	    "QComboBox, QPlainTextEdit { background: #1b2537; color: #edf2fb; border: 1px solid #3b4b65;"
	    " border-radius: 6px; padding: 8px; }"
	    "QComboBox QAbstractItemView { background: #1b2537; color: #edf2fb; selection-background-color: #344764; }"
	    "QPushButton { background: #26344b; color: #edf2fb; border: 1px solid #3b4b65;"
	    " border-radius: 6px; padding: 8px 12px; }"
	    "QPushButton:hover { background: #344764; }"
	    "QPushButton:default { background: #a9c5ff; color: #111c32; }"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(24, 24, 24, 24);
	layout->setSpacing(14);
	auto* heading = new QLabel(tr("Choose what you want the local player to do. No typing needed."), this);
	heading->setWordWrap(true);
	layout->addWidget(heading);
	m_goals = new QComboBox(this);
	m_goals->setObjectName("localGoalPreset");
	m_goals->setAccessibleName(tr("Gameplay goal"));
	for (const LocalModelGoal& goal : goals) {
		m_goals->addItem(goal.label, goal.id);
		m_goals->setItemData(m_goals->count() - 1, goal.goal, Qt::UserRole + 1);
	}
	int previousIndex = m_goals->findData(previousGoal);
	if (previousIndex >= 0) {
		m_goals->setCurrentIndex(previousIndex);
	}
	layout->addWidget(m_goals);
	m_preview = new QPlainTextEdit(this);
	m_preview->setObjectName("localGoalPreview");
	m_preview->setAccessibleName(tr("Selected goal instructions"));
	m_preview->setReadOnly(true);
	m_preview->setPlainText(selectedGoalText());
	layout->addWidget(m_preview, 1);
	connect(m_goals, qOverload<int>(&QComboBox::currentIndexChanged), this,
	        [this]() { m_preview->setPlainText(selectedGoalText()); });
	auto* notice = new QLabel(tr("Supports verified English Emerald only. Make an in-game save first. "
	                             "Unknown dialogue, healing, move learning and evolution may require your help. "
	                             "Enabling the model may download missing model/runtime files."),
	                          this);
	notice->setWordWrap(true);
	layout->addWidget(notice);
	m_lunaHelp = new QCheckBox(tr("Ask GPT-6 Luna when stuck (paid; shares decoded game state)"), this);
	m_lunaHelp->setObjectName("localLunaHelp");
	bool lunaConfigured =
	    !qEnvironmentVariable("LUNA_API_KEY").isEmpty() && !qEnvironmentVariable("LUNA_MODEL").isEmpty();
	m_lunaHelp->setEnabled(lunaConfigured);
	m_lunaHelp->setChecked(lunaConfigured && qEnvironmentVariable("LUNA_HELP") == "1");
	m_lunaHelp->setToolTip(tr("Set LUNA_API_KEY and LUNA_MODEL to your provider's GPT-6 Luna API model ID before "
	                          "launching mGBA. Optional LUNA_BASE_URL selects an OpenAI-compatible endpoint. "
	                          "At most 20 remote calls per run. Unknown prompts still require manual input."));
	layout->addWidget(m_lunaHelp);
	auto* buttons = new QDialogButtonBox(this);
	auto* start = buttons->addButton(tr("Start with selected goal"), QDialogButtonBox::AcceptRole);
	start->setObjectName("startLocalGoal");
	start->setDefault(true);
	start->setEnabled(!goals.isEmpty());
	auto* enable = buttons->addButton(tr("Enable model only"), QDialogButtonBox::ActionRole);
	enable->setObjectName("enableLocalModelOnly");
	auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
	cancel->setObjectName("cancelLocalGoal");
	connect(start, &QPushButton::clicked, this, &QDialog::accept);
	connect(enable, &QPushButton::clicked, this, [this]() { done(EnableOnly); });
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	layout->addWidget(buttons);
}

QString LocalModelGoalDialog::selectedGoalId() const {
	return m_goals->currentData().toString();
}

QString LocalModelGoalDialog::selectedGoalText() const {
	return m_goals->currentData(Qt::UserRole + 1).toString();
}

bool LocalModelGoalDialog::lunaHelpEnabled() const {
	return m_lunaHelp->isEnabled() && m_lunaHelp->isChecked();
}

QVector<LocalModelGoal> LocalModelGoalDialog::loadGoals(const QString& path, QString* error) {
	if (error) {
		error->clear();
	}
	auto fail = [error](const QString& message) -> QVector<LocalModelGoal> {
		if (error) {
			*error = message;
		}
		return {};
	};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
		return fail(tr("Could not read gameplay goal presets. Restore tools/local-model/goals.json and retry."));
	}
	QJsonParseError parseError;
	QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	QJsonObject root = document.object();
	QJsonArray definitions = root.value("goals").toArray();
	if (parseError.error != QJsonParseError::NoError || root.value("version").toInt() != 1 || definitions.isEmpty() ||
	    definitions.size() > 32) {
		return fail(tr("Gameplay goal presets have an invalid format."));
	}
	QVector<LocalModelGoal> goals;
	QSet<QString> ids;
	QRegularExpression idPattern(QStringLiteral("^[a-z][a-z0-9-]{0,47}$"));
	for (const QJsonValue& value : definitions) {
		QJsonObject item = value.toObject();
		LocalModelGoal goal { item.value("id").toString(), item.value("label").toString(),
			                  item.value("goal").toString() };
		if (!idPattern.match(goal.id).hasMatch() || ids.contains(goal.id) || goal.label.trimmed().isEmpty() ||
		    goal.label.size() > 80 || goal.goal.trimmed().isEmpty() || goal.goal.size() > 512) {
			return fail(tr("Gameplay goal presets contain an invalid or duplicate entry."));
		}
		ids.insert(goal.id);
		goals.append(goal);
	}
	return goals;
}
