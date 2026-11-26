/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Willow {

// The ROM library: which files in the chosen folder are games, which title
// screen to show for each, and the ids remote controllers use. Paths are
// UTF-8 with '/' separators on every platform. Drawing the library and
// caching artwork belong to the frontend.
namespace Library {

// "GBA", "GBC" or "GB" for a ROM file extension (lower case, no dot), else "".
std::string romPlatform(const std::string& suffix);
// macOS archive litter: __MACOSX folders, .DS_Store and ._ resource forks.
bool isMetadata(const std::string& relativePath);

// File name pieces as QFileInfo names them: the suffix follows the last dot.
std::string fileName(const std::string& path);
std::string suffix(const std::string& path);
std::string completeBaseName(const std::string& path);

struct Game {
	std::string path;
	std::string relativePath;
	// The file name without its extension, as the library shows it.
	std::string title;
	// GBA, GBC or GB for a ROM, ZIP or 7Z for an archive.
	std::string platform;
	// The name and platform used to find artwork. For an archive these come
	// from the first ROM inside it, and the platform may be empty.
	std::string romName;
	std::string artworkPlatform;
	bool compressed = false;
};
// False for files that are not games. Archives are opened to read their
// file names only; nothing is extracted.
bool classify(const std::string& path, const std::string& relativePath, Game* game);

// QDir::cleanPath: '/' separators, no "." or ".." parts, no repeated or
// trailing slashes.
std::string cleanPath(const std::string& path);
// cleanPath of the absolute path.
std::string absolutePath(const std::string& path);
// A stable id that does not reveal the path: the first 20 hex digits of the
// SHA-1 of the absolute path.
std::string gameId(const std::string& path);

// A comparable form of a title: release tags such as "(USA)" and "(Rev 1)"
// removed, accents folded, lower case letters and digits only.
std::string artworkKey(const std::string& title);

// Title-screen names from res/library-titles.json, which maps "GB", "GBC"
// and "GBA" to libretro-thumbnails Named_Titles file names.
class ArtworkCatalog {
public:
	explicit ArtworkCatalog(const std::string& json);
	std::size_t size() const { return m_size; }
	// The best matching title screen's URL, or "" when none matches.
	std::string url(const std::string& title, const std::string& platform) const;

private:
	std::unordered_map<std::string, std::vector<std::string>> m_names;
	std::size_t m_size = 0;
};

// Lists readable files under a folder, a few at a time so a frontend can
// keep its UI responsive. Hidden files and folders and symbolic links are
// skipped, so the scan never leaves the folder or loops.
class Scanner {
public:
	explicit Scanner(const std::string& folder);
	bool done() const { return m_done; }
	// The next file, or false once every file has been listed.
	bool next(std::string* path, std::string* relativePath);

private:
	void advance();

	std::string m_folder;
	std::filesystem::path m_root;
	std::filesystem::recursive_directory_iterator m_iterator;
	bool m_done = true;
};

} // namespace Library
} // namespace Willow
