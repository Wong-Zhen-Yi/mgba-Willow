/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "nlohmann/json.hpp"

namespace QGBA {

// libwillow speaks nlohmann::json; the Qt frontend's bridge speaks QJson.
inline QJsonObject toQJson(const nlohmann::json& value) {
	return QJsonDocument::fromJson(QByteArray::fromStdString(value.dump())).object();
}

inline QJsonValue toQJsonValue(const nlohmann::json& value) {
	return QJsonDocument::fromJson(QByteArray::fromStdString(nlohmann::json::array({ value }).dump())).array().at(0);
}

inline nlohmann::json toWillowJson(const QJsonValue& value) {
	const QByteArray text = QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact);
	return nlohmann::json::parse(text.constData(), text.constData() + text.size()).at(0);
}

}
