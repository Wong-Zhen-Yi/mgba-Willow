/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "willow/Library.h"

#include <mgba-util/sha1.h>
#include <mgba-util/vfs.h>

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Willow;

namespace {

struct Fold {
	uint32_t codePoint;
	char ascii;
};

// Non-ASCII code points whose canonical decomposition (NFD), lower-cased,
// keeps exactly one ASCII letter or digit: é -> e, Å -> a, K (Kelvin) -> k.
// Generated with Python 3.13 (Unicode 15.1):
//   for cp in range(0x80, 0x110000):
//       s = ''.join(c for c in unicodedata.normalize('NFD', chr(cp)).lower()
//                   if c in string.ascii_lowercase + string.digits)
const Fold FoldTable[] = {
	{ 0x00C0, 'a' }, { 0x00C1, 'a' }, { 0x00C2, 'a' }, { 0x00C3, 'a' }, { 0x00C4, 'a' }, { 0x00C5, 'a' },
	{ 0x00C7, 'c' }, { 0x00C8, 'e' }, { 0x00C9, 'e' }, { 0x00CA, 'e' }, { 0x00CB, 'e' }, { 0x00CC, 'i' },
	{ 0x00CD, 'i' }, { 0x00CE, 'i' }, { 0x00CF, 'i' }, { 0x00D1, 'n' }, { 0x00D2, 'o' }, { 0x00D3, 'o' },
	{ 0x00D4, 'o' }, { 0x00D5, 'o' }, { 0x00D6, 'o' }, { 0x00D9, 'u' }, { 0x00DA, 'u' }, { 0x00DB, 'u' },
	{ 0x00DC, 'u' }, { 0x00DD, 'y' }, { 0x00E0, 'a' }, { 0x00E1, 'a' }, { 0x00E2, 'a' }, { 0x00E3, 'a' },
	{ 0x00E4, 'a' }, { 0x00E5, 'a' }, { 0x00E7, 'c' }, { 0x00E8, 'e' }, { 0x00E9, 'e' }, { 0x00EA, 'e' },
	{ 0x00EB, 'e' }, { 0x00EC, 'i' }, { 0x00ED, 'i' }, { 0x00EE, 'i' }, { 0x00EF, 'i' }, { 0x00F1, 'n' },
	{ 0x00F2, 'o' }, { 0x00F3, 'o' }, { 0x00F4, 'o' }, { 0x00F5, 'o' }, { 0x00F6, 'o' }, { 0x00F9, 'u' },
	{ 0x00FA, 'u' }, { 0x00FB, 'u' }, { 0x00FC, 'u' }, { 0x00FD, 'y' }, { 0x00FF, 'y' }, { 0x0100, 'a' },
	{ 0x0101, 'a' }, { 0x0102, 'a' }, { 0x0103, 'a' }, { 0x0104, 'a' }, { 0x0105, 'a' }, { 0x0106, 'c' },
	{ 0x0107, 'c' }, { 0x0108, 'c' }, { 0x0109, 'c' }, { 0x010A, 'c' }, { 0x010B, 'c' }, { 0x010C, 'c' },
	{ 0x010D, 'c' }, { 0x010E, 'd' }, { 0x010F, 'd' }, { 0x0112, 'e' }, { 0x0113, 'e' }, { 0x0114, 'e' },
	{ 0x0115, 'e' }, { 0x0116, 'e' }, { 0x0117, 'e' }, { 0x0118, 'e' }, { 0x0119, 'e' }, { 0x011A, 'e' },
	{ 0x011B, 'e' }, { 0x011C, 'g' }, { 0x011D, 'g' }, { 0x011E, 'g' }, { 0x011F, 'g' }, { 0x0120, 'g' },
	{ 0x0121, 'g' }, { 0x0122, 'g' }, { 0x0123, 'g' }, { 0x0124, 'h' }, { 0x0125, 'h' }, { 0x0128, 'i' },
	{ 0x0129, 'i' }, { 0x012A, 'i' }, { 0x012B, 'i' }, { 0x012C, 'i' }, { 0x012D, 'i' }, { 0x012E, 'i' },
	{ 0x012F, 'i' }, { 0x0130, 'i' }, { 0x0134, 'j' }, { 0x0135, 'j' }, { 0x0136, 'k' }, { 0x0137, 'k' },
	{ 0x0139, 'l' }, { 0x013A, 'l' }, { 0x013B, 'l' }, { 0x013C, 'l' }, { 0x013D, 'l' }, { 0x013E, 'l' },
	{ 0x0143, 'n' }, { 0x0144, 'n' }, { 0x0145, 'n' }, { 0x0146, 'n' }, { 0x0147, 'n' }, { 0x0148, 'n' },
	{ 0x014C, 'o' }, { 0x014D, 'o' }, { 0x014E, 'o' }, { 0x014F, 'o' }, { 0x0150, 'o' }, { 0x0151, 'o' },
	{ 0x0154, 'r' }, { 0x0155, 'r' }, { 0x0156, 'r' }, { 0x0157, 'r' }, { 0x0158, 'r' }, { 0x0159, 'r' },
	{ 0x015A, 's' }, { 0x015B, 's' }, { 0x015C, 's' }, { 0x015D, 's' }, { 0x015E, 's' }, { 0x015F, 's' },
	{ 0x0160, 's' }, { 0x0161, 's' }, { 0x0162, 't' }, { 0x0163, 't' }, { 0x0164, 't' }, { 0x0165, 't' },
	{ 0x0168, 'u' }, { 0x0169, 'u' }, { 0x016A, 'u' }, { 0x016B, 'u' }, { 0x016C, 'u' }, { 0x016D, 'u' },
	{ 0x016E, 'u' }, { 0x016F, 'u' }, { 0x0170, 'u' }, { 0x0171, 'u' }, { 0x0172, 'u' }, { 0x0173, 'u' },
	{ 0x0174, 'w' }, { 0x0175, 'w' }, { 0x0176, 'y' }, { 0x0177, 'y' }, { 0x0178, 'y' }, { 0x0179, 'z' },
	{ 0x017A, 'z' }, { 0x017B, 'z' }, { 0x017C, 'z' }, { 0x017D, 'z' }, { 0x017E, 'z' }, { 0x01A0, 'o' },
	{ 0x01A1, 'o' }, { 0x01AF, 'u' }, { 0x01B0, 'u' }, { 0x01CD, 'a' }, { 0x01CE, 'a' }, { 0x01CF, 'i' },
	{ 0x01D0, 'i' }, { 0x01D1, 'o' }, { 0x01D2, 'o' }, { 0x01D3, 'u' }, { 0x01D4, 'u' }, { 0x01D5, 'u' },
	{ 0x01D6, 'u' }, { 0x01D7, 'u' }, { 0x01D8, 'u' }, { 0x01D9, 'u' }, { 0x01DA, 'u' }, { 0x01DB, 'u' },
	{ 0x01DC, 'u' }, { 0x01DE, 'a' }, { 0x01DF, 'a' }, { 0x01E0, 'a' }, { 0x01E1, 'a' }, { 0x01E6, 'g' },
	{ 0x01E7, 'g' }, { 0x01E8, 'k' }, { 0x01E9, 'k' }, { 0x01EA, 'o' }, { 0x01EB, 'o' }, { 0x01EC, 'o' },
	{ 0x01ED, 'o' }, { 0x01F0, 'j' }, { 0x01F4, 'g' }, { 0x01F5, 'g' }, { 0x01F8, 'n' }, { 0x01F9, 'n' },
	{ 0x01FA, 'a' }, { 0x01FB, 'a' }, { 0x0200, 'a' }, { 0x0201, 'a' }, { 0x0202, 'a' }, { 0x0203, 'a' },
	{ 0x0204, 'e' }, { 0x0205, 'e' }, { 0x0206, 'e' }, { 0x0207, 'e' }, { 0x0208, 'i' }, { 0x0209, 'i' },
	{ 0x020A, 'i' }, { 0x020B, 'i' }, { 0x020C, 'o' }, { 0x020D, 'o' }, { 0x020E, 'o' }, { 0x020F, 'o' },
	{ 0x0210, 'r' }, { 0x0211, 'r' }, { 0x0212, 'r' }, { 0x0213, 'r' }, { 0x0214, 'u' }, { 0x0215, 'u' },
	{ 0x0216, 'u' }, { 0x0217, 'u' }, { 0x0218, 's' }, { 0x0219, 's' }, { 0x021A, 't' }, { 0x021B, 't' },
	{ 0x021E, 'h' }, { 0x021F, 'h' }, { 0x0226, 'a' }, { 0x0227, 'a' }, { 0x0228, 'e' }, { 0x0229, 'e' },
	{ 0x022A, 'o' }, { 0x022B, 'o' }, { 0x022C, 'o' }, { 0x022D, 'o' }, { 0x022E, 'o' }, { 0x022F, 'o' },
	{ 0x0230, 'o' }, { 0x0231, 'o' }, { 0x0232, 'y' }, { 0x0233, 'y' }, { 0x1E00, 'a' }, { 0x1E01, 'a' },
	{ 0x1E02, 'b' }, { 0x1E03, 'b' }, { 0x1E04, 'b' }, { 0x1E05, 'b' }, { 0x1E06, 'b' }, { 0x1E07, 'b' },
	{ 0x1E08, 'c' }, { 0x1E09, 'c' }, { 0x1E0A, 'd' }, { 0x1E0B, 'd' }, { 0x1E0C, 'd' }, { 0x1E0D, 'd' },
	{ 0x1E0E, 'd' }, { 0x1E0F, 'd' }, { 0x1E10, 'd' }, { 0x1E11, 'd' }, { 0x1E12, 'd' }, { 0x1E13, 'd' },
	{ 0x1E14, 'e' }, { 0x1E15, 'e' }, { 0x1E16, 'e' }, { 0x1E17, 'e' }, { 0x1E18, 'e' }, { 0x1E19, 'e' },
	{ 0x1E1A, 'e' }, { 0x1E1B, 'e' }, { 0x1E1C, 'e' }, { 0x1E1D, 'e' }, { 0x1E1E, 'f' }, { 0x1E1F, 'f' },
	{ 0x1E20, 'g' }, { 0x1E21, 'g' }, { 0x1E22, 'h' }, { 0x1E23, 'h' }, { 0x1E24, 'h' }, { 0x1E25, 'h' },
	{ 0x1E26, 'h' }, { 0x1E27, 'h' }, { 0x1E28, 'h' }, { 0x1E29, 'h' }, { 0x1E2A, 'h' }, { 0x1E2B, 'h' },
	{ 0x1E2C, 'i' }, { 0x1E2D, 'i' }, { 0x1E2E, 'i' }, { 0x1E2F, 'i' }, { 0x1E30, 'k' }, { 0x1E31, 'k' },
	{ 0x1E32, 'k' }, { 0x1E33, 'k' }, { 0x1E34, 'k' }, { 0x1E35, 'k' }, { 0x1E36, 'l' }, { 0x1E37, 'l' },
	{ 0x1E38, 'l' }, { 0x1E39, 'l' }, { 0x1E3A, 'l' }, { 0x1E3B, 'l' }, { 0x1E3C, 'l' }, { 0x1E3D, 'l' },
	{ 0x1E3E, 'm' }, { 0x1E3F, 'm' }, { 0x1E40, 'm' }, { 0x1E41, 'm' }, { 0x1E42, 'm' }, { 0x1E43, 'm' },
	{ 0x1E44, 'n' }, { 0x1E45, 'n' }, { 0x1E46, 'n' }, { 0x1E47, 'n' }, { 0x1E48, 'n' }, { 0x1E49, 'n' },
	{ 0x1E4A, 'n' }, { 0x1E4B, 'n' }, { 0x1E4C, 'o' }, { 0x1E4D, 'o' }, { 0x1E4E, 'o' }, { 0x1E4F, 'o' },
	{ 0x1E50, 'o' }, { 0x1E51, 'o' }, { 0x1E52, 'o' }, { 0x1E53, 'o' }, { 0x1E54, 'p' }, { 0x1E55, 'p' },
	{ 0x1E56, 'p' }, { 0x1E57, 'p' }, { 0x1E58, 'r' }, { 0x1E59, 'r' }, { 0x1E5A, 'r' }, { 0x1E5B, 'r' },
	{ 0x1E5C, 'r' }, { 0x1E5D, 'r' }, { 0x1E5E, 'r' }, { 0x1E5F, 'r' }, { 0x1E60, 's' }, { 0x1E61, 's' },
	{ 0x1E62, 's' }, { 0x1E63, 's' }, { 0x1E64, 's' }, { 0x1E65, 's' }, { 0x1E66, 's' }, { 0x1E67, 's' },
	{ 0x1E68, 's' }, { 0x1E69, 's' }, { 0x1E6A, 't' }, { 0x1E6B, 't' }, { 0x1E6C, 't' }, { 0x1E6D, 't' },
	{ 0x1E6E, 't' }, { 0x1E6F, 't' }, { 0x1E70, 't' }, { 0x1E71, 't' }, { 0x1E72, 'u' }, { 0x1E73, 'u' },
	{ 0x1E74, 'u' }, { 0x1E75, 'u' }, { 0x1E76, 'u' }, { 0x1E77, 'u' }, { 0x1E78, 'u' }, { 0x1E79, 'u' },
	{ 0x1E7A, 'u' }, { 0x1E7B, 'u' }, { 0x1E7C, 'v' }, { 0x1E7D, 'v' }, { 0x1E7E, 'v' }, { 0x1E7F, 'v' },
	{ 0x1E80, 'w' }, { 0x1E81, 'w' }, { 0x1E82, 'w' }, { 0x1E83, 'w' }, { 0x1E84, 'w' }, { 0x1E85, 'w' },
	{ 0x1E86, 'w' }, { 0x1E87, 'w' }, { 0x1E88, 'w' }, { 0x1E89, 'w' }, { 0x1E8A, 'x' }, { 0x1E8B, 'x' },
	{ 0x1E8C, 'x' }, { 0x1E8D, 'x' }, { 0x1E8E, 'y' }, { 0x1E8F, 'y' }, { 0x1E90, 'z' }, { 0x1E91, 'z' },
	{ 0x1E92, 'z' }, { 0x1E93, 'z' }, { 0x1E94, 'z' }, { 0x1E95, 'z' }, { 0x1E96, 'h' }, { 0x1E97, 't' },
	{ 0x1E98, 'w' }, { 0x1E99, 'y' }, { 0x1EA0, 'a' }, { 0x1EA1, 'a' }, { 0x1EA2, 'a' }, { 0x1EA3, 'a' },
	{ 0x1EA4, 'a' }, { 0x1EA5, 'a' }, { 0x1EA6, 'a' }, { 0x1EA7, 'a' }, { 0x1EA8, 'a' }, { 0x1EA9, 'a' },
	{ 0x1EAA, 'a' }, { 0x1EAB, 'a' }, { 0x1EAC, 'a' }, { 0x1EAD, 'a' }, { 0x1EAE, 'a' }, { 0x1EAF, 'a' },
	{ 0x1EB0, 'a' }, { 0x1EB1, 'a' }, { 0x1EB2, 'a' }, { 0x1EB3, 'a' }, { 0x1EB4, 'a' }, { 0x1EB5, 'a' },
	{ 0x1EB6, 'a' }, { 0x1EB7, 'a' }, { 0x1EB8, 'e' }, { 0x1EB9, 'e' }, { 0x1EBA, 'e' }, { 0x1EBB, 'e' },
	{ 0x1EBC, 'e' }, { 0x1EBD, 'e' }, { 0x1EBE, 'e' }, { 0x1EBF, 'e' }, { 0x1EC0, 'e' }, { 0x1EC1, 'e' },
	{ 0x1EC2, 'e' }, { 0x1EC3, 'e' }, { 0x1EC4, 'e' }, { 0x1EC5, 'e' }, { 0x1EC6, 'e' }, { 0x1EC7, 'e' },
	{ 0x1EC8, 'i' }, { 0x1EC9, 'i' }, { 0x1ECA, 'i' }, { 0x1ECB, 'i' }, { 0x1ECC, 'o' }, { 0x1ECD, 'o' },
	{ 0x1ECE, 'o' }, { 0x1ECF, 'o' }, { 0x1ED0, 'o' }, { 0x1ED1, 'o' }, { 0x1ED2, 'o' }, { 0x1ED3, 'o' },
	{ 0x1ED4, 'o' }, { 0x1ED5, 'o' }, { 0x1ED6, 'o' }, { 0x1ED7, 'o' }, { 0x1ED8, 'o' }, { 0x1ED9, 'o' },
	{ 0x1EDA, 'o' }, { 0x1EDB, 'o' }, { 0x1EDC, 'o' }, { 0x1EDD, 'o' }, { 0x1EDE, 'o' }, { 0x1EDF, 'o' },
	{ 0x1EE0, 'o' }, { 0x1EE1, 'o' }, { 0x1EE2, 'o' }, { 0x1EE3, 'o' }, { 0x1EE4, 'u' }, { 0x1EE5, 'u' },
	{ 0x1EE6, 'u' }, { 0x1EE7, 'u' }, { 0x1EE8, 'u' }, { 0x1EE9, 'u' }, { 0x1EEA, 'u' }, { 0x1EEB, 'u' },
	{ 0x1EEC, 'u' }, { 0x1EED, 'u' }, { 0x1EEE, 'u' }, { 0x1EEF, 'u' }, { 0x1EF0, 'u' }, { 0x1EF1, 'u' },
	{ 0x1EF2, 'y' }, { 0x1EF3, 'y' }, { 0x1EF4, 'y' }, { 0x1EF5, 'y' }, { 0x1EF6, 'y' }, { 0x1EF7, 'y' },
	{ 0x1EF8, 'y' }, { 0x1EF9, 'y' }, { 0x212A, 'k' }, { 0x212B, 'a' },
};

bool isAsciiLetter(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isAsciiDigit(char c) {
	return c >= '0' && c <= '9';
}

// A regular-expression word character without Unicode properties.
bool isWordChar(char c) {
	return isAsciiLetter(c) || isAsciiDigit(c) || c == '_';
}

char asciiLower(char c) {
	return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

std::string asciiLower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](char c) { return asciiLower(c); });
	return text;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
	return a.size() == b.size() &&
	    std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return asciiLower(x) == asciiLower(y); });
}

bool startsWith(const std::string& text, const char* prefix) {
	return text.compare(0, std::strlen(prefix), prefix) == 0;
}

// Decodes one UTF-8 code point, or returns false for a malformed byte.
bool decode(const std::string& text, std::size_t* i, uint32_t* codePoint) {
	const auto lead = static_cast<unsigned char>(text[*i]);
	int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
	if (!length || *i + length > text.size()) {
		++*i;
		return false;
	}
	uint32_t value = length == 1 ? lead : lead & (0x7F >> length);
	for (int k = 1; k < length; ++k) {
		const auto next = static_cast<unsigned char>(text[*i + k]);
		if ((next & 0xC0) != 0x80) {
			++*i;
			return false;
		}
		value = (value << 6) | (next & 0x3F);
	}
	*i += length;
	*codePoint = value;
	return true;
}

// The contents of a release tag such as "(USA, Europe)" or "(Rev 1)".
bool isReleaseTag(const std::string& content) {
	const std::string lower = asciiLower(content);
	if (lower == "sgb enhanced" || lower == "cgb+sgb enhanced" || lower == "gb compatible")
		return true;
	auto versionNumber = [&](std::size_t from) {
		return lower.size() > from &&
		    std::all_of(lower.begin() + from, lower.end(), [](char c) { return isAsciiDigit(c) || c == '.'; });
	};
	if ((startsWith(lower, "rev ") && versionNumber(4)) || (startsWith(lower, "v") && versionNumber(1)))
		return true;
	// A region or two-letter language, then ", Word" or ",Word" repeated.
	static const char* const regions[] = { "usa",    "europe",      "japan",  "world", "australia", "canada",
	                                       "france", "germany",     "italy",  "spain", "korea",     "china",
	                                       "brazil", "netherlands", "sweden", "u",     "e",         "j" };
	std::size_t comma = lower.find(',');
	const std::string first = lower.substr(0, comma);
	const bool region = (first.size() == 2 && isAsciiLetter(first[0]) && isAsciiLetter(first[1])) ||
	    std::any_of(std::begin(regions), std::end(regions), [&](const char* name) { return first == name; });
	if (!region)
		return false;
	while (comma != std::string::npos) {
		std::size_t start = comma + 1;
		if (start < lower.size() && lower[start] == ' ')
			++start;
		comma = lower.find(',', start);
		const std::size_t end = comma == std::string::npos ? lower.size() : comma;
		if (start == end || !std::all_of(lower.begin() + start, lower.begin() + end, isAsciiLetter))
			return false;
	}
	return true;
}

std::string removeReleaseTags(const std::string& title) {
	std::string result;
	for (std::size_t i = 0; i < title.size();) {
		if (title[i] == '(') {
			const std::size_t close = title.find(')', i + 1);
			if (close != std::string::npos) {
				const std::string content = title.substr(i + 1, close - i - 1);
				if (content.find('(') == std::string::npos && isReleaseTag(content)) {
					i = close + 1;
					continue;
				}
			}
		}
		result += title[i++];
	}
	return result;
}

// Removes every whole-word, case-insensitive match of word.
std::string removeWord(const std::string& text, const std::string& word) {
	std::string result;
	for (std::size_t i = 0; i < text.size();) {
		const bool boundaryBefore = i == 0 || !isWordChar(text[i - 1]);
		const std::size_t end = i + word.size();
		if (boundaryBefore && end <= text.size() && equalsIgnoreCase(text.substr(i, word.size()), word) &&
		    (end == text.size() || !isWordChar(text[end]))) {
			i = end;
			continue;
		}
		result += text[i++];
	}
	return result;
}

std::string removeAll(std::string text, const std::string& needle) {
	for (std::size_t at; (at = text.find(needle)) != std::string::npos;)
		text.erase(at, needle.size());
	return text;
}

// Whole-word region names in a title's parenthesized tags, in order.
std::vector<std::string> titleRegions(const std::string& title) {
	static const char* const names[] = { "USA",    "Europe", "Japan",   "World",       "Australia",
	                                     "Canada", "France", "Germany", "Italy",       "Spain",
	                                     "Korea",  "China",  "Brazil",  "Netherlands", "Sweden" };
	std::vector<std::string> regions;
	for (std::size_t open = title.find('('); open != std::string::npos; open = title.find('(', open)) {
		const std::size_t close = title.find(')', open + 1);
		if (close == std::string::npos)
			break;
		const std::string tag = title.substr(open, close - open + 1);
		for (std::size_t i = 0; i < tag.size();) {
			const char* match = nullptr;
			if (i == 0 || !isWordChar(tag[i - 1])) {
				for (const char* name : names) {
					const std::size_t end = i + std::strlen(name);
					if (tag.compare(i, std::strlen(name), name) == 0 && (end == tag.size() || !isWordChar(tag[end]))) {
						match = name;
						break;
					}
				}
			}
			if (match) {
				regions.emplace_back(match);
				i += regions.back().size();
			} else {
				++i;
			}
		}
		open = close + 1;
	}
	return regions;
}

std::string percentEncode(const std::string& text) {
	static const char digits[] = "0123456789ABCDEF";
	std::string result;
	for (char c : text) {
		if (isAsciiLetter(c) || isAsciiDigit(c) || c == '-' || c == '.' || c == '_' || c == '~') {
			result += c;
		} else {
			const auto byte = static_cast<unsigned char>(c);
			result += '%';
			result += digits[byte >> 4];
			result += digits[byte & 0xF];
		}
	}
	return result;
}

// Paths are UTF-8 everywhere in mGBA, including on Windows.
std::filesystem::path fromUtf8(const std::string& path) {
#ifdef __cpp_char8_t
	return std::filesystem::path(reinterpret_cast<const char8_t*>(path.data()),
	                             reinterpret_cast<const char8_t*>(path.data() + path.size()));
#else
	return std::filesystem::u8path(path);
#endif
}

std::string toUtf8(const std::filesystem::path& path) {
	const auto text = path.generic_u8string();
	return std::string(text.begin(), text.end());
}

bool isSeparator(char c) {
#ifdef _WIN32
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

bool isAbsolute(const std::string& path) {
#ifdef _WIN32
	if (path.size() >= 3 && isAsciiLetter(path[0]) && path[1] == ':' && isSeparator(path[2]))
		return true;
	return path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]);
#else
	return !path.empty() && path[0] == '/';
#endif
}

} // namespace

std::string Library::romPlatform(const std::string& suffix) {
	if (suffix == "gba" || suffix == "agb")
		return "GBA";
	if (suffix == "gbc")
		return "GBC";
	if (suffix == "gb" || suffix == "sgb")
		return "GB";
	return { };
}

bool Library::isMetadata(const std::string& relativePath) {
	std::size_t start = 0;
	while (start <= relativePath.size()) {
		std::size_t end = start;
		while (end < relativePath.size() && !isSeparator(relativePath[end]))
			++end;
		const std::string part = relativePath.substr(start, end - start);
		if (part == "__MACOSX" || part == ".DS_Store" || startsWith(part, "._"))
			return true;
		start = end + 1;
	}
	return false;
}

std::string Library::fileName(const std::string& path) {
	std::size_t start = path.size();
	while (start > 0 && !isSeparator(path[start - 1]))
		--start;
	return path.substr(start);
}

std::string Library::suffix(const std::string& path) {
	const std::string name = fileName(path);
	const std::size_t dot = name.rfind('.');
	return dot == std::string::npos ? std::string() : name.substr(dot + 1);
}

std::string Library::completeBaseName(const std::string& path) {
	const std::string name = fileName(path);
	return name.substr(0, name.rfind('.'));
}

namespace {

// The format of a supported archive holding at least one ROM, with the first
// ROM's name and platform. Only file names are read.
std::string archivePlatform(const std::string& path, std::string* romName, std::string* platform) {
	std::string format;
	const std::string extension = asciiLower(Library::suffix(path));
#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
	if (extension == "zip")
		format = "ZIP";
#endif
#ifdef USE_LZMA
	if (extension == "7z")
		format = "7Z";
#endif
	if (format.empty())
		return { };
	VDir* archive = VDirOpenArchive(path.c_str());
	if (!archive)
		return { };
	bool hasRom = false;
	while (VDirEntry* entry = archive->listNext(archive)) {
		const std::string name = entry->name(entry);
		if (entry->type(entry) == VFS_DIRECTORY || Library::isMetadata(name))
			continue;
		const std::string inner = asciiLower(Library::suffix(name));
		if (!Library::romPlatform(inner).empty() || inner == "rom" || inner == "bin" || inner == "mb") {
			hasRom = true;
			*romName = Library::completeBaseName(name);
			*platform = Library::romPlatform(inner);
			break;
		}
	}
	archive->close(archive);
	return hasRom ? format : std::string();
}

} // namespace

bool Library::classify(const std::string& path, const std::string& relativePath, Game* game) {
	if (isMetadata(relativePath))
		return false;
	Game result;
	result.path = path;
	result.relativePath = relativePath;
	result.title = completeBaseName(path);
	result.platform = romPlatform(asciiLower(suffix(path)));
	result.artworkPlatform = result.platform;
	result.romName = result.title;
	result.compressed = result.platform.empty();
	if (result.compressed)
		result.platform = archivePlatform(path, &result.romName, &result.artworkPlatform);
	if (result.platform.empty())
		return false;
	*game = std::move(result);
	return true;
}

std::string Library::cleanPath(const std::string& path) {
	if (path.empty())
		return { };
	std::string prefix;
	std::size_t i = 0;
	bool rooted = false;
#ifdef _WIN32
	if (path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1])) {
		// A network share: ".." never climbs above //server/share.
		prefix = "//";
		i = 2;
		for (int part = 0; part < 2 && i < path.size(); ++part) {
			std::size_t end = i;
			while (end < path.size() && !isSeparator(path[end]))
				++end;
			const std::string name = path.substr(i, end - i);
			if (name.empty() || name == "." || name == "..")
				break;
			prefix += (part ? "/" : "") + name;
			rooted = part == 1;
			i = std::min(end + 1, path.size() + 1);
		}
		if (rooted)
			prefix += '/';
	} else if (path.size() >= 2 && isAsciiLetter(path[0]) && path[1] == ':') {
		prefix = path.substr(0, 2);
		i = 2;
		if (i < path.size() && isSeparator(path[i])) {
			prefix += '/';
			++i;
		}
	} else
#endif
	    if (isSeparator(path[0])) {
		prefix = "/";
		i = 1;
	}
	rooted = rooted || (!prefix.empty() && prefix.back() == '/');
	std::vector<std::string> parts;
	while (i <= path.size()) {
		std::size_t end = i;
		while (end < path.size() && !isSeparator(path[end]))
			++end;
		const std::string part = path.substr(i, end - i);
		if (part == "..") {
			if (!parts.empty() && parts.back() != "..")
				parts.pop_back();
			else if (!rooted)
				parts.push_back(part);
		} else if (!part.empty() && part != ".") {
			parts.push_back(part);
		}
		i = end + 1;
	}
	std::string result = prefix;
	if (!parts.empty() && !result.empty() && result.back() != '/' && result.back() != ':')
		result += '/';
	for (std::size_t k = 0; k < parts.size(); ++k) {
		if (k)
			result += '/';
		result += parts[k];
	}
	if (result.size() > 3 && result.back() == '/' && result.compare(0, 2, "//") == 0)
		result.pop_back();
	return result.empty() ? "." : result;
}

std::string Library::absolutePath(const std::string& path) {
	std::string absolute = path;
	if (!isAbsolute(path)) {
		std::error_code error;
		const std::string current = toUtf8(std::filesystem::current_path(error));
#ifdef _WIN32
		// "/Roms" means the Roms folder on the current drive.
		if (!path.empty() && isSeparator(path[0]) && current.size() >= 2 && current[1] == ':')
			absolute = current.substr(0, 2) + path;
		else
#endif
			absolute = current + "/" + path;
	}
	absolute = cleanPath(absolute);
#ifdef _WIN32
	// Qt writes Windows drive letters in upper case.
	if (absolute.size() >= 2 && absolute[1] == ':' && absolute[0] >= 'a' && absolute[0] <= 'z')
		absolute[0] -= 'a' - 'A';
#endif
	return absolute;
}

std::string Library::gameId(const std::string& path) {
	const std::string absolute = absolutePath(path);
	uint8_t digest[20];
	sha1Buffer(absolute.data(), absolute.size(), digest);
	static const char digits[] = "0123456789abcdef";
	std::string id;
	for (int i = 0; i < 10; ++i) {
		id += digits[digest[i] >> 4];
		id += digits[digest[i] & 0xF];
	}
	return id;
}

std::string Library::artworkKey(const std::string& title) {
	// Ignore release metadata, but keep unknown tags such as ROM hack names.
	std::string text = removeReleaseTags(title);
	text = removeAll(text, "[!]");
	text = removeWord(text, "version");
	text = removeWord(text, "and");
	std::string key;
	for (std::size_t i = 0; i < text.size();) {
		uint32_t codePoint = 0;
		if (!decode(text, &i, &codePoint))
			continue;
		if (codePoint < 0x80) {
			const char c = asciiLower(char(codePoint));
			if (isAsciiLetter(c) || isAsciiDigit(c))
				key += c;
			continue;
		}
		const auto fold = std::lower_bound(std::begin(FoldTable), std::end(FoldTable), codePoint,
		                                   [](const Fold& entry, uint32_t value) { return entry.codePoint < value; });
		if (fold != std::end(FoldTable) && fold->codePoint == codePoint)
			key += fold->ascii;
	}
	if (key == "pokemonyellow")
		key = "pokemonyellowspecialpikachuedition";
	return key;
}

Library::ArtworkCatalog::ArtworkCatalog(const std::string& json) {
	const auto systems = nlohmann::json::parse(json, nullptr, false);
	if (!systems.is_object())
		return;
	for (const auto& [system, names] : systems.items()) {
		if (!names.is_array())
			continue;
		for (const auto& entry : names) {
			const std::string name = entry.is_string() ? entry.get<std::string>() : std::string();
			m_names[system + ":" + artworkKey(name)].push_back(name);
			++m_size;
		}
	}
}

std::string Library::ArtworkCatalog::url(const std::string& title, const std::string& platform) const {
	std::string matchedPlatform = platform;
	std::string key = matchedPlatform + ":" + artworkKey(title);
	// Some dual-mode Game Boy Color releases are distributed with a .gb extension.
	if (!m_names.count(key) && (platform == "GB" || platform == "GBC")) {
		matchedPlatform = platform == "GB" ? "GBC" : "GB";
		key = matchedPlatform + ":" + artworkKey(title);
	}
	const auto found = m_names.find(key);
	if (found == m_names.end())
		return { };
	const std::vector<std::string> regions = titleRegions(title);
	std::string best;
	int bestScore = -1;
	for (const std::string& name : found->second) {
		if (!regions.empty() && std::none_of(regions.begin(), regions.end(), [&](const std::string& region) {
			    return name.find(region) != std::string::npos;
		    }))
			continue;
		int score = name.find("(USA") != std::string::npos ? 3 :
		    name.find("(World") != std::string::npos       ? 2 :
		    name.find("(Europe") != std::string::npos      ? 1 :
		                                                     0;
		if (name.find("(Rev ") == std::string::npos)
			++score;
		// Keep a supplied region or revision instead of borrowing another release's title screen.
		if (equalsIgnoreCase(name, title))
			score += 100;
		if (score > bestScore) {
			best = name;
			bestScore = score;
		}
	}
	if (best.empty())
		return { };
	const char* system = matchedPlatform == "GBA" ? "Nintendo_-_Game_Boy_Advance" :
	    matchedPlatform == "GBC"                  ? "Nintendo_-_Game_Boy_Color" :
	                                                "Nintendo_-_Game_Boy";
	return std::string("https://raw.githubusercontent.com/libretro-thumbnails/") + system + "/master/Named_Titles/" +
	    percentEncode(best) + ".png";
}

namespace {

bool hidden(const std::filesystem::path& path, const std::string& name) {
#ifdef _WIN32
	(void) name;
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN);
#else
	if (!name.empty() && name[0] == '.')
		return true;
#ifdef __APPLE__
	struct stat info;
	if (lstat(path.c_str(), &info) == 0 && (info.st_flags & UF_HIDDEN))
		return true;
#endif
	return false;
#endif
}

// Symbolic links, and on Windows junctions and other reparse points.
bool link(const std::filesystem::path& path, const std::filesystem::file_status& status) {
	if (std::filesystem::is_symlink(status))
		return true;
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
	(void) path;
	return false;
#endif
}

bool readable(const std::filesystem::path& path) {
#ifdef _WIN32
	(void) path;
	return true;
#else
	return access(path.c_str(), R_OK) == 0;
#endif
}

} // namespace

Library::Scanner::Scanner(const std::string& folder)
    : m_folder(cleanPath(folder))
    , m_root(fromUtf8(m_folder)) {
	std::error_code error;
	if (m_folder.empty() || !std::filesystem::is_directory(m_root, error))
		return;
	m_iterator = std::filesystem::recursive_directory_iterator(
	    m_root, std::filesystem::directory_options::skip_permission_denied, error);
	m_done = error || m_iterator == std::filesystem::recursive_directory_iterator();
}

void Library::Scanner::advance() {
	std::error_code error;
	m_iterator.increment(error);
	if (error || m_iterator == std::filesystem::recursive_directory_iterator())
		m_done = true;
}

bool Library::Scanner::next(std::string* path, std::string* relativePath) {
	while (!m_done) {
		const std::filesystem::path entry = m_iterator->path();
		std::error_code error;
		const auto status = m_iterator->symlink_status(error);
		const std::string name = toUtf8(entry.filename());
		bool found = false;
		if (error || link(entry, status) || hidden(entry, name)) {
			if (!error && std::filesystem::is_directory(status))
				m_iterator.disable_recursion_pending();
		} else if (std::filesystem::is_regular_file(status) && readable(entry)) {
			*relativePath = toUtf8(entry.lexically_relative(m_root));
			*path = m_folder + (m_folder.back() == '/' ? "" : "/") + *relativePath;
			found = true;
		}
		advance();
		if (found)
			return true;
	}
	return false;
}
