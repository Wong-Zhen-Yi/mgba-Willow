/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "willow/Library.h"

#include <mgba-util/vfs.h>
#include <mgba/flags.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#define CHECK(condition)                                                           \
	do {                                                                           \
		if (!(condition)) {                                                        \
			std::cerr << "Failed line " << __LINE__ << ": " << #condition << '\n'; \
			std::abort();                                                          \
		}                                                                          \
	} while (0)

using namespace Willow;
namespace fs = std::filesystem;

namespace {

void testNames() {
	CHECK(Library::romPlatform("gba") == "GBA" && Library::romPlatform("agb") == "GBA");
	CHECK(Library::romPlatform("gbc") == "GBC");
	CHECK(Library::romPlatform("gb") == "GB" && Library::romPlatform("sgb") == "GB");
	CHECK(Library::romPlatform("GBA").empty() && Library::romPlatform("nds").empty() &&
	      Library::romPlatform("").empty());

	CHECK(Library::isMetadata("__MACOSX/nested/Game.gba"));
	CHECK(Library::isMetadata("nested/.DS_Store"));
	CHECK(Library::isMetadata("._Game.gba"));
	CHECK(Library::isMetadata("a/._b/c.gba"));
	CHECK(!Library::isMetadata("Game.gba"));
	CHECK(!Library::isMetadata("nested/_Game.gba"));
	CHECK(!Library::isMetadata("MACOSX/Game.gba"));

	CHECK(Library::fileName("a/b/Game.gba") == "Game.gba");
	CHECK(Library::suffix("a/b/Game.v1.GBA") == "GBA");
	CHECK(Library::completeBaseName("a/b/Game.v1.GBA") == "Game.v1");
	CHECK(Library::suffix("a.dir/noext").empty() && Library::completeBaseName("a.dir/noext") == "noext");
	CHECK(Library::suffix(".gba") == "gba" && Library::completeBaseName(".gba").empty());
}

void testPaths() {
	CHECK(Library::cleanPath("/roms/./a/../Emerald.gba") == "/roms/Emerald.gba");
	CHECK(Library::cleanPath("/roms//a///") == "/roms/a");
	CHECK(Library::cleanPath("/..") == "/");
	CHECK(Library::cleanPath("/") == "/");
	CHECK(Library::cleanPath("a/../..") == "..");
	CHECK(Library::cleanPath("a/..") == ".");
	CHECK(Library::cleanPath("").empty());
	CHECK(Library::gameId("/roms/Emerald.gba").size() == 20);
#ifdef _WIN32
	CHECK(Library::cleanPath("C:\\Roms\\sub\\..\\Emerald.gba") == "C:/Roms/Emerald.gba");
	CHECK(Library::cleanPath("C:/..") == "C:/");
	CHECK(Library::cleanPath("//server/share/../x.gb") == "//server/share/x.gb");
	CHECK(Library::cleanPath("//server/share/") == "//server/share");
	CHECK(Library::cleanPath("//server/../x.gb") == "//server/../x.gb");
	CHECK(Library::absolutePath("c:/Roms/Emerald.gba") == "C:/Roms/Emerald.gba");
	// The same ids as the Qt build, which hashed QDir::cleanPath(absoluteFilePath()).
	CHECK(Library::gameId("C:/Roms/Emerald.gba") == "d1fc7cf80b3fd586b647");
	CHECK(Library::gameId("c:\\Roms\\x\\..\\Emerald.gba") == "d1fc7cf80b3fd586b647");
#else
	CHECK(Library::gameId("/roms/Emerald.gba") == "a4647b7c842161836642");
	CHECK(Library::gameId("/roms/x/../Emerald.gba") == "a4647b7c842161836642");
#endif
	const std::string relative = Library::absolutePath("relative/Game.gba");
	CHECK(relative.size() > std::string("/relative/Game.gba").size());
	CHECK(relative.compare(relative.size() - 18, 18, "/relative/Game.gba") == 0);
}

void testArtworkKeys() {
	CHECK(Library::artworkKey("Pokemon - Emerald Version (USA, Europe)") == "pokemonemerald");
	CHECK(Library::artworkKey("Pok\xC3\xA9mon - Emerald Version (USA, Europe)") == "pokemonemerald");
	CHECK(Library::artworkKey("POK\xC3\x89MON EMERALD") == "pokemonemerald");
	CHECK(Library::artworkKey("Pokemon Yellow") == "pokemonyellowspecialpikachuedition");
	CHECK(Library::artworkKey("Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (CGB+SGB Enhanced)") ==
	      "pokemonyellowspecialpikachuedition");
	CHECK(Library::artworkKey("Zelda (U) [!]") == "zelda");
	CHECK(Library::artworkKey("Game (En,Fr,De) (Rev 1.1) (V1.0) (SGB Enhanced) (GB Compatible)") == "game");
	CHECK(Library::artworkKey("Game (Fr, De ,It)") == "gamefrdeit");
	CHECK(Library::artworkKey("Game (Proto) (Beta)") == "gameprotobeta");
	CHECK(Library::artworkKey("Game (Hack by Me)") == "gamehackbyme");
	CHECK(Library::artworkKey("Andromeda and Version_2 versionX") == "andromedaversion2versionx");
	CHECK(Library::artworkKey("a(b(USA)c)") == "abc");
	CHECK(Library::artworkKey("(USA") == "usa");
	CHECK(Library::artworkKey("\xE2\x84\xAA\xC3\x85 \xC3\x86 \xC3\x9F \xEF\xBC\xA1 \xE1\xBA\xA0") == "kaa");
	CHECK(Library::artworkKey("").empty());
}

void testCatalog() {
	const Library::ArtworkCatalog catalog(R"json({
		"GB": ["Tetris (World) (Rev 1)", "Tetris (World)", "Tetris (Japan)"],
		"GBC": ["Pokemon - Crystal Version (USA, Europe) (Rev 1)", "Pokemon - Crystal Version (USA, Europe)",
		        "Pokemon - Crystal Version (Japan)"],
		"GBA": ["Pok\u00e9mon Emerald & Ruby (Europe)", "Golden Sun (USA, Europe)", "Golden Sun (Germany)"]
	})json");
	CHECK(catalog.size() == 9);
	const std::string base = "https://raw.githubusercontent.com/libretro-thumbnails/";
	CHECK(catalog.url("Tetris", "GB") == base + "Nintendo_-_Game_Boy/master/Named_Titles/Tetris%20%28World%29.png");
	// A named region keeps that release; an exact name wins over the scores.
	CHECK(catalog.url("Tetris (Japan)", "GB") ==
	      base + "Nintendo_-_Game_Boy/master/Named_Titles/Tetris%20%28Japan%29.png");
	CHECK(catalog.url("Tetris (World) (Rev 1)", "GB") ==
	      base + "Nintendo_-_Game_Boy/master/Named_Titles/Tetris%20%28World%29%20%28Rev%201%29.png");
	CHECK(catalog.url("Tetris (Korea)", "GB").empty());
	// A .gb copy of a Game Boy Color game finds the GBC title screen.
	CHECK(catalog.url("Pokemon Crystal", "GB") ==
	      base +
	          "Nintendo_-_Game_Boy_Color/master/Named_Titles/"
	          "Pokemon%20-%20Crystal%20Version%20%28USA%2C%20Europe%29.png");
	CHECK(catalog.url("Golden Sun (Germany)", "GBA") ==
	      base + "Nintendo_-_Game_Boy_Advance/master/Named_Titles/Golden%20Sun%20%28Germany%29.png");
	CHECK(catalog.url("golden sun", "GBA").find("USA") != std::string::npos);
	CHECK(catalog.url("Pokemon Emerald & Ruby", "GBA") ==
	      base +
	          "Nintendo_-_Game_Boy_Advance/master/Named_Titles/Pok%C3%A9mon%20Emerald%20%26%20Ruby%20%28Europe%29.png");
	CHECK(catalog.url("Golden Sun", "GB").empty());
	CHECK(catalog.url("Unknown", "GBA").empty());
	CHECK(Library::ArtworkCatalog("not json").size() == 0);
	CHECK(Library::ArtworkCatalog("[]").url("Tetris", "GB").empty());
}

void write(const fs::path& path) {
	fs::create_directories(path.parent_path());
	std::ofstream(path) << "synthetic fixture";
}

std::string utf8(const fs::path& path) {
	const auto text = path.generic_u8string();
	return std::string(text.begin(), text.end());
}

void hide(const fs::path& path) {
#ifdef _WIN32
	SetFileAttributesW(path.c_str(), GetFileAttributesW(path.c_str()) | FILE_ATTRIBUTE_HIDDEN);
#else
	(void) path;
#endif
}

void testScan() {
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("willow-library-test-" + std::to_string(stamp));
	write(root / "Emerald.GBA");
	write(root / "nested" / "Blue (USA).gb");
	write(root / "nested" / "deeper" / "Crystal.gbc");
	write(root / "nested" / "notes.txt");
	write(root / "._Emerald.gba");
	write(root / "__MACOSX" / "x" / "Sidecar.gba");
	write(root / "noext");
	write(root / "target" / "Linked.gba");
#ifdef _WIN32
	write(root / "hidden" / "Secret.gba");
	hide(root / "hidden");
	write(root / "Hidden.gba");
	hide(root / "Hidden.gba");
#else
	write(root / ".hidden" / "Secret.gba");
	write(root / ".Hidden.gba");
#endif
	std::error_code ignored;
	fs::create_directory_symlink(root / "target", root / "link", ignored);
	fs::create_symlink(root / "Emerald.GBA", root / "Alias.gba", ignored);
#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
	{
		const std::string zip = utf8(root / "Compressed.ZIP");
		VDir* archive = VDirOpenZip(zip.c_str(), O_CREAT | O_TRUNC | O_WRONLY);
		CHECK(archive);
		for (const char* member : { "__MACOSX/._Inside.gba", "folder/Inside.v2.GBA", "folder/Second.gb" }) {
			VFile* file = archive->openFile(archive, member, O_CREAT | O_TRUNC | O_WRONLY);
			CHECK(file);
			CHECK(file->write(file, "x", 1) == 1);
			CHECK(file->close(file));
		}
		CHECK(archive->close(archive));
		const std::string empty = utf8(root / "Empty.zip");
		archive = VDirOpenZip(empty.c_str(), O_CREAT | O_TRUNC | O_WRONLY);
		CHECK(archive);
		VFile* file = archive->openFile(archive, "readme.txt", O_CREAT | O_TRUNC | O_WRONLY);
		CHECK(file && file->write(file, "x", 1) == 1 && file->close(file));
		CHECK(archive->close(archive));
	}
#endif

	const std::string folder = utf8(root);
	Library::Scanner scanner(folder + "/");
	std::set<std::string> listed;
	std::set<std::string> games;
	std::string path, relativePath;
	while (scanner.next(&path, &relativePath)) {
		CHECK(path == folder + "/" + relativePath);
		listed.insert(relativePath);
		Library::Game game;
		if (Library::classify(path, relativePath, &game))
			games.insert(game.relativePath + "|" + game.title + "|" + game.platform + "|" + game.romName + "|" +
			             game.artworkPlatform + "|" + (game.compressed ? "zip" : "rom"));
	}
	CHECK(scanner.done());
	CHECK(!scanner.next(&path, &relativePath));
	// Hidden files and folders and links are never listed.
	CHECK(!listed.count("Hidden.gba") && !listed.count(".Hidden.gba"));
	CHECK(std::none_of(listed.begin(), listed.end(), [](const std::string& name) {
		return name.find("Secret") != std::string::npos || name.rfind("link/", 0) == 0 || name == "Alias.gba";
	}));
	CHECK(listed.count("nested/notes.txt") && listed.count("noext") && listed.count("target/Linked.gba"));
	std::set<std::string> expected = {
		"Emerald.GBA|Emerald|GBA|Emerald|GBA|rom",
		"nested/Blue (USA).gb|Blue (USA)|GB|Blue (USA)|GB|rom",
		"nested/deeper/Crystal.gbc|Crystal|GBC|Crystal|GBC|rom",
		"target/Linked.gba|Linked|GBA|Linked|GBA|rom",
	};
#if defined(USE_LIBZIP) || defined(USE_MINIZIP)
	expected.insert("Compressed.ZIP|Compressed|ZIP|Inside.v2|GBA|zip");
#endif
	for (const auto& game : games)
		std::cout << "  " << game << '\n';
	CHECK(games == expected);

	Library::Game game;
	CHECK(!Library::classify(folder + "/__MACOSX/x/Sidecar.gba", "__MACOSX/x/Sidecar.gba", &game));
	CHECK(!Library::classify(folder + "/missing.zip", "missing.zip", &game));

	CHECK(Library::Scanner(folder + "/missing").done());
	CHECK(Library::Scanner("").done());
	fs::remove_all(root, ignored);
}

} // namespace

int main() {
	testNames();
	testPaths();
	testArtworkKeys();
	testCatalog();
	testScan();
	std::cout << "library: ok\n";
	return 0;
}
