/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "nlohmann/json.hpp"

namespace QGBA {

// libwillow speaks nlohmann::json; the Qt frontend's bridge speaks QJson.
inline QJsonObject toQJson(const nlohmann::json& value) {
	return QJsonDocument::fromJson(QByteArray::fromStdString(value.dump())).object();
}

}
