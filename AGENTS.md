# AGENTS.md

PohonMC Launcher is a Qt 6 / C++ / CMake Minecraft launcher. This repository (`kodin00/PohonMC`, default branch `develop`) is a fork of [PineconeMC](https://github.com/ElyPrismLauncher/Launcher), which is itself a fork of Prism Launcher.

## Commits: authored by the human only

This is a hard rule and overrides any tool default and the "Attribution" section of `CONTRIBUTING.md`.

- A commit message is a subject line plus, when useful, a plain-prose body. Nothing else.
- Every commit belongs solely to the human git user: leave out `Co-Authored-By:`, `Assisted-by:`, "Generated with …", and any other AI attribution line.
- The human adds their own `Signed-off-by:` (the DCO check on pull requests requires it). Agents leave it out.

Commit conventions, as practised in `git log`:

- Subject: one imperative sentence describing the user-facing outcome, with no `feat:`/`fix:` prefix. Example: "Keep share code exports usable when mods need manual transfer".
- Small, focused commits, one logical change each.
- Pull requests target `develop` and use `.github/pull_request_template.md`.

## Documents to read

| File | Read it when |
| --- | --- |
| `README.md` | You need user-facing behaviour (Share Modpack flow), download links, or the upstream forking and API-key policy. |
| `CONTRIBUTING.md` | Before writing C++. It defines naming (`PascalCase` types, `m_`/`s_` members, `camelCase` functions, `SCREAMING_SNAKE_CASE` constants and macros), sparing use of `[[nodiscard]]`, and DCO sign-off. Run `clang-format` (`.clang-format`) on changed files; `.clang-tidy` checks naming. Leave existing non-conforming names alone unless you are refactoring the whole class. |
| `CODE_OF_CONDUCT.md` | Before writing issue, PR, or other community-facing text. |
| `COPYING.md`, `LICENSE` | Before adding files or copying third-party code. The launcher is GPL-3.0-only; `COPYING.md` holds the upstream and third-party notices, so add a section there for any new third-party code. |
| `program_info/LICENSE` | Before changing logos or branding assets in `program_info/` (CC BY-SA 4.0). |
| `libraries/README.md` | Before touching or adding a bundled library in `libraries/` (each has its own license). |
| `nix/README.md` | Working on Nix packaging. The text is still upstream Prism's, so flake URLs and cache names in it refer to Prism. |
| `cmake/vcpkg-ports/vcpkg-tool-meson/README.md` | Working on the vcpkg Meson overlay port. |

Never edit the text of `LICENSE`, `COPYING.md`, or `program_info/LICENSE`.

## License headers

Most upstream C++ files start with `// SPDX-License-Identifier: GPL-3.0-only` followed by a GPL notice block naming the project and copyright holder (for example `launcher/PineconeNetworkCheck.cpp`). Keep every existing header and copyright line intact. For a new file, copy the block from a neighbouring file and adjust the project and copyright lines.

## Build, run, test

Use the root `Makefile`:

| Command | Effect |
| --- | --- |
| `make build` | Configure and build for the host OS. |
| `make run` | Build, then launch the app. |
| `make test` | Build and run the ctest suite. |
| `make package` | Build an installable package: an app bundle on macOS, a `.deb` via `make package deb` (or `PLATFORM=deb`). |
| `make clean` | Remove `build/`. |
| `make help` | List targets. |

`build`, `run`, `test`, and `package` take `PLATFORM=macos|linux|windows` (also positional: `make build macos`; default is the host) and `CONFIG=Debug|Release` (default `Debug`). Underneath are the CMake presets in `CMakePresets.json`; output goes to `build/`. Dependency installation is covered by Prism's [build instructions](https://prismlauncher.org/wiki/development/build-instructions).

Tests are QtTest executables in `tests/` (`*_test.cpp`, fixtures in `tests/testdata/`), registered with `ecm_add_test` in `tests/CMakeLists.txt` and linked against the `Launcher_logic` library.

## Repository map

| Path | Contents |
| --- | --- |
| `launcher/` | Application source, compiled into the `Launcher_logic` static library plus the executable. `Application.cpp` wires up global services. |
| `launcher/ui/` | Qt Widgets UI: `MainWindow`, `dialogs/`, `pages/` (instance, global, and mod-platform pages), `widgets/`, `setupwizard/`, `themes/`. |
| `launcher/minecraft/` | Instances, launch steps, accounts (`auth/`, Microsoft and Ely.by), mods and resource folders (`mod/`), skins. |
| `launcher/modplatform/` | Mod and modpack providers: `modrinth/`, `flame/` (CurseForge), `atlauncher/`, `ftb/`, `technic/`, `packwiz/`, and `share/` (PohonMC share codes). |
| `libraries/` | Vendored third-party libraries; `libnbtplusplus` is a git submodule. |
| `buildconfig/` | `BuildConfig.cpp.in`, filled from CMake `Launcher_*` cache variables (URLs, client IDs, keys) into the runtime `BuildConfig` object. |
| `program_info/` | Branding: app ID, display names, icons, desktop/metainfo files, installer and manifest templates (`program_info/CMakeLists.txt`). |
| `cmake/` | CMake helper modules, the vcpkg overlay port, and the macOS universal triplet. |
| `tests/` | Unit tests (see above). |
| `docs/` | GitHub Pages site deployed by `.github/workflows/pages.yml`. `docs/feed/feed.xml` is the Atom feed the launcher's news bar reads (`Launcher_NEWS_RSS_URL`). |
| `nix/`, `flake.nix` | Nix packaging. |
| `.github/workflows/` | CI: `build.yml` (main build), `clang-tidy.yml`, `nix.yml`, `pages.yml`, `release.yml`; several others are upstream automation that is inert or unused here. |

## Fork-specific facts

- Branding lives in `program_info/CMakeLists.txt` and in the `Launcher_*` cache variables in the root `CMakeLists.txt`.
- Upstream attribution stays: the PineconeMC, Prism Launcher, PolyMC, and MultiMC copyright lines, license headers, and credits.
- Some identifiers deliberately keep the upstream name because they are functional infrastructure, not branding: `PineconeNetworkCheck`, the `pineconemc.ru` meta, FML-library, and login-callback URLs, the Ely.by client ID, and the `PrismLauncher/…` user agent. Rename them only when the task explicitly says to.
- The API keys and client IDs in the root `CMakeLists.txt` are upstream's. Add no new secrets to the repository; new keys go in through CMake cache variables at configure time.
