# Repository Guidelines

## Project Structure & Module Organization

This is the mGBA emulator with Willow's local MCP controls and Emerald-specific adapters. Emulator code lives in `src/` (`arm/`, `gb/`, `gba/`, `core/`, `util/`); public headers are in `include/mgba/`. Frontends live under `src/platform/`, especially `qt/` and `sdl/`. The Node.js MCP server, setup scripts, and tests are in `tools/mcp/`. Assets are in `res/`, documentation in `doc/`, and emulation regression fixtures in `cinema/`. Keep vendored dependencies in `src/third-party/` separate from project changes.

## Build, Test, and Development Commands

Install a C/C++ toolchain, CMake, and frontend dependencies as described in `README.md`; Windows developers can use MSYS2.

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_AI_TESTS=ON`: configure a debug build with standalone Qt AI tests.
- `cmake --build build --parallel`: compile configured targets.
- `ctest --test-dir build/qt -R "platform-qt-" --output-on-failure`: run Qt tests.
- `cmake -S . -B build -DBUILD_SUITE=ON`: enable the broader cmocka suite; build, then run `ctest --test-dir build --output-on-failure`.
- In `tools/mcp/`, run `npm ci`, then `npm test`; `npm start` launches the stdio MCP server. Requires Node.js 20+.
- Double-click `Launch mGBA.cmd` to build and open the Windows app.

## Coding Style & Naming Conventions

Follow `CONTRIBUTING.md` and `.clang-format`: tabs for C/C++ indentation, four-column tab width, attached braces, and a 120-column limit. Use camelCase variables, capitalized C structs, struct-prefixed functions, underscore-prefixed static helpers, and uppercase enum constants. Qt classes belong in `QGBA`, with `m_` members and `s_` statics. Match existing two-space indentation in MCP `.mjs` files. Format C/C++ changes with clang-format.

## Testing Guidelines

Place C tests in the component's `test/` directory, Qt tests in `src/platform/qt/test/`, and Node tests in `tools/mcp/test/*.test.mjs` using `node:test`. Add regression cases for changed behavior; no numeric coverage threshold is documented. Use synthetic Emerald fixtures where possible. Live smoke tests advance gameplay and create checkpoints: run them only against disposable sessions.

## Commit & Pull Request Guidelines

Use concise imperative subjects. History includes plain Willow subjects and component prefixes such as `Core:`, `Qt:`, and `GB Video:`; prefer a component prefix for code changes. Describe the problem, resulting behavior, and validation; link relevant issues and include screenshots for UI changes. Preserve MPL-2.0 licensing. Consult `CONTRIBUTING.md` before upstream submissions, which reject AI-generated code.
