/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "LocalModelGoalDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

using namespace QGBA;

#define CHECK(condition)                                                      \
	do {                                                                      \
		if (!(condition)) {                                                   \
			std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
			std::exit(1);                                                     \
		}                                                                     \
	} while (0)

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	CHECK(app.arguments().size() >= 2);
	QString error;
	auto goals = LocalModelGoalDialog::loadGoals(app.arguments().at(1), &error);
	CHECK(error.isEmpty());
	CHECK(goals.size() == 4);
	CHECK(goals.at(0).id == "progress-story");
	CHECK(goals.at(0).label == "Progress the story");
	CHECK(goals.at(1).id == "level-up-pokemon");
	CHECK(goals.at(1).label == QString::fromUtf8("Level up Pok\xc3\xa9mon"));
	for (const auto& goal : goals) {
		CHECK(!goal.goal.trimmed().isEmpty());
		CHECK(goal.goal.size() <= 512);
	}
	qunsetenv("LUNA_API_KEY");
	qunsetenv("LUNA_MODEL");
	qunsetenv("LUNA_HELP");
	LocalModelGoalDialog picker(goals, "level-up-pokemon");
	CHECK(!picker.lunaHelpEnabled());
	CHECK(!picker.findChild<QCheckBox*>("localLunaHelp")->isEnabled());
	auto* combo = picker.findChild<QComboBox*>("localGoalPreset");
	auto* preview = picker.findChild<QPlainTextEdit*>("localGoalPreview");
	CHECK(combo && preview && preview->isReadOnly());
	CHECK(picker.result() == QDialog::Rejected);
	CHECK(picker.selectedGoalId() == "level-up-pokemon");
	CHECK(preview->toPlainText() == goals.at(1).goal);
	combo->setCurrentIndex(0);
	CHECK(picker.selectedGoalId() == "progress-story");
	CHECK(picker.selectedGoalText() == goals.at(0).goal);
	CHECK(preview->toPlainText() == goals.at(0).goal);
	picker.findChild<QPushButton*>("startLocalGoal")->click();
	CHECK(picker.result() == QDialog::Accepted);

	LocalModelGoalDialog modelOnly(goals, "not-a-current-preset");
	CHECK(modelOnly.selectedGoalId() == "progress-story");
	modelOnly.findChild<QPushButton*>("enableLocalModelOnly")->click();
	CHECK(modelOnly.result() == LocalModelGoalDialog::EnableOnly);
	LocalModelGoalDialog cancel(goals, QString());
	cancel.findChild<QPushButton*>("cancelLocalGoal")->click();
	CHECK(cancel.result() == QDialog::Rejected);

	qputenv("LUNA_API_KEY", "synthetic-test-key");
	qputenv("LUNA_MODEL", "provider-luna-test-id");
	LocalModelGoalDialog assistance(goals, QString());
	auto* luna = assistance.findChild<QCheckBox*>("localLunaHelp");
	CHECK(luna && luna->isEnabled() && !assistance.lunaHelpEnabled());
	luna->setChecked(true);
	CHECK(assistance.lunaHelpEnabled());
	qputenv("LUNA_HELP", "1");
	LocalModelGoalDialog enabled(goals, QString());
	CHECK(enabled.lunaHelpEnabled());

	QTemporaryDir temporary;
	CHECK(temporary.isValid());
	auto invalid = [&](const QByteArray& contents) {
		QString path = temporary.path() + "/goals.json";
		QFile file(path);
		CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		file.write(contents);
		file.close();
		CHECK(LocalModelGoalDialog::loadGoals(path, &error).isEmpty());
		CHECK(!error.isEmpty());
	};
	invalid("not JSON");
	invalid("{\"version\":1,\"goals\":[]}");
	invalid(QByteArray(65537, ' '));
	QJsonObject definition { { "id", "same" }, { "label", "Test" }, { "goal", "Synthetic goal" } };
	QJsonObject root { { "version", 1 }, { "goals", QJsonArray { definition, definition } } };
	invalid(QJsonDocument(root).toJson());
	definition.insert("goal", QString(513, 'x'));
	root.insert("goals", QJsonArray { definition });
	invalid(QJsonDocument(root).toJson());
	definition.insert("goal", "Synthetic goal");
	definition.insert("id", "../invalid");
	root.insert("goals", QJsonArray { definition });
	invalid(QJsonDocument(root).toJson());
	CHECK(LocalModelGoalDialog::loadGoals(temporary.path() + "/missing.json", &error).isEmpty());
	CHECK(!error.isEmpty());
	CHECK(LocalModelGoalDialog::loadGoals(app.arguments().at(1), &error).size() == goals.size());
	CHECK(error.isEmpty());
	std::puts("Local gameplay goal tests passed");
	return 0;
}
