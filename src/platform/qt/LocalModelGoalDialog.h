/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;

namespace QGBA {

struct LocalModelGoal {
	QString id;
	QString label;
	QString goal;
};

class LocalModelGoalDialog : public QDialog {
public:
	enum { EnableOnly = 2 };

	LocalModelGoalDialog(const QVector<LocalModelGoal>& goals, const QString& previousGoal, QWidget* parent = nullptr);
	QString selectedGoalId() const;
	QString selectedGoalText() const;
	bool lunaHelpEnabled() const;
	static QVector<LocalModelGoal> loadGoals(const QString& path, QString* error);

private:
	QCheckBox* m_lunaHelp;
	QComboBox* m_goals;
	QPlainTextEdit* m_preview;
};

}
