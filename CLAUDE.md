# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

LogSlayer is a terminal log viewer (TUI) for watching one or more log sources as they change. It opens local files, folders of log files, zstd-compressed files, and SSH log sources, then lets you search, filter, jump between matches, align timestamps across sources, and manage sources from an interactive command palette. Built on FTXUI for the terminal UI.

## Build / test

CMake project driven entirely by presets (`CMakePresets.json`). Requires CMake 3.21+, C++17, and a Boost **source tree** (Boost is not vendored).

Prerequisites:
- `git submodule update --init --recursive` — note `libs/ftxui_components` uses an SSH GitHub URL, so submodule init needs GitHub SSH access unless that URL is changed locally.
- Set `BOOST_ROOT` (env var, read by presets via `$penv{BOOST_ROOT}`, or `-DBOOST_ROOT=<path>`) to a Boost source tree. CMake `add_subdirectory`s it and builds: asio, system, program_options, log, filesystem, thread, regex, date_time, atomic. A missing `BOOST_ROOT` is a hard configure error.

Configure + build. On Windows, **prefer the clang preset over msvc**:
```sh
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
```
Presets: `windows-clang-{debug,release}` (preferred on Windows), `windows-msvc-{debug,release}`, `linux-gcc-{debug,release}`, `linux-gcc-tsan` (ThreadSanitizer, CTest disabled). Build output goes to `out/build/<preset>/`.

Run all tests:
```sh
ctest --preset windows-clang-debug
```

Run a single test (the test binary is `unit_tests`, GoogleTest):
```sh
ctest --preset windows-clang-debug -R <TestNameRegex>
# or run the binary directly:
out/build/windows-clang-debug/bin/unit_tests --gtest_filter=Suite.Case
```

`compile_commands.json` is exported (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`) for clangd.

## Code layout

Application code lives under `code/slayerlog/`. Everything else under `libs/` is a vendored submodule dependency (ftxui, ftxui_components, googletest, log4cplus, zstd, nlohmann_json, simpleini, eestv) — do not edit those. `eestv` (`libs/eestv`) is the author's own utility library (`eestv_lib` target). Tests live under `tests/unit_tests/`.

The app target `LogSlayer` and the `unit_tests` target both compile the shared `${SLAYERLOG_CORE_SOURCES}` list defined in `code/slayerlog/CMakeLists.txt` (exported to PARENT_SCOPE) — so adding a source file means adding it there, and it is automatically picked up by the tests.

## Architecture

The core is a polling pipeline from raw log sources to rendered terminal text, with a mutex-guarded model and a background watcher thread. `main.cpp` wires it all together.

**Data pipeline (the central abstraction layers):**

1. **Watchers** (`watchers/`) — produce raw lines from a transport. `LogWatcher`/`LogWatcherBase` interface; implementations include `file_watcher`, `zstd_file_watcher`, `ssh_tail_watcher` (over `process_pipe` + `stream_line_buffer`). Created via `log_watcher_factory`. Watchers are thread-safe (base class holds the mutex).

2. **Tracked sources** (`tracked_sources/`) — own watchers and turn raw lines into parsed `LogEntry` objects (timestamp + message + source/sequence metadata). `TrackedSourceBase` is the abstract base; `tracked_source_file`/`tracked_source_folder` are concrete; `tracked_source_factory` builds them. `AllTrackedSources` aggregates all sources, merges their entries into one time-ordered `IndexedVector` (`all_lines()`), and exposes `poll()` which returns the index of the first new/changed line. Owns timestamp formats and per-source timestamp offset/format.

3. **Processed sources** (`tracked_sources/all_processed_sources.hpp`) — `AllProcessedSources` is the view model. It takes entries from `AllTrackedSources` and applies all user-facing transforms: include/exclude filters, column hiding, identical-line deduplication, hide-before-line, pause, source labels, original-vs-aligned time. It distinguishes **all entries** from **visible rows** and renders entries to display strings. Append vs. replace paths matter: `append_from_sources` for streaming growth, `replace_from_sources` when earlier lines changed.

4. **LogController** (`LogView/log_controller.hpp`) — renders `AllProcessedSources` into a double-buffered text buffer driving a `TextViewController` (from ftxui_components). `rebuild_view` = full re-render (after filter/column/reset changes); `sync_view` = incremental append (streaming). Also owns find (search) state, time-alignment, and text selection/clipboard.

**Two log views.** There are two parallel view implementations: the original `LogView/` (`LogViewComponent` + `LogController` + `AllProcessedSources`, full feature set) and a newer simpler `LogView2/` (`LogView2Component` + `LogView2Data`, a thin read-only adapter over `AllTrackedSources`). `main.cpp` currently shows both side by side in a horizontal container, each with its own command palette controller. When touching view logic, be clear which view you mean.

**Threading.** `main.cpp` spawns one background watcher thread (`start_watcher_thread`) that sleeps `poll_interval_ms`, calls `tracked_sources.poll()`, and on new data updates `AllProcessedSources` + `LogController` and posts a custom FTXUI event to redraw. A single `model_mutex` guards the whole model (tracked sources, processed sources, controller). **Any code reading or mutating the model must hold `model_mutex`** — commands receive it via `CommandContext::model_mutex`. The TSan preset exists to catch violations.

**Commands / command palette** (`commands/`) — the user-facing action system (Ctrl+P palette). `Command` is the interface (`commands/command.hpp`); concrete commands live in `commands/implementations/`. `register_commands` (`command_registrar`) instantiates them all with a `CommandContext` (references to processed sources, controller, tracked sources, screen, notifier, model mutex, background tasks, settings path). `CommandManager` holds them; `CommandPaletteController`/`CommandPaletteModel`/`CommandPaletteView` drive the palette UI; `command_history` persists recent commands. Commands can run interactively (own event loop, custom render, widgets in `commands/command_widgets/`). **Adding a feature usually means adding a command implementation + registering it in `command_registrar.cpp` + adding it to `SLAYERLOG_CORE_SOURCES`.**

**Settings** (`settings_store`, `settings_ini`) — INI-backed (simpleini) persistence for settings, command history, and timestamp formats. Platform-specific path resolved by `default_settings_file_path()` (LOCALAPPDATA / Application Support / XDG_CONFIG_HOME, with a CWD fallback). Settings loading is resilient: failures disable saves for the run rather than aborting.

**Timestamps** (`timestamp/`) — `TimestampFormatCatalog` holds the list of recognized formats (seeded from settings or `default_timestamp_formats()`); `SourceTimestampParser` parses per source; per-source offsets enable cross-source time alignment (`time_alignment_controller`, `align_time_command`).

**Notifications** (`notifications/`) — `Notifier`/notification sink abstraction; the FTXUI implementation (`ftxui_toast_notification_sink`) renders toasts via ftxui_components' `ToastHostComponent`.

`debug_log` (log4cplus, configured by `log4cplus.ini` copied next to the executable post-build) is a developer diagnostic log written to a temp path — separate from the logs the app *views*. Use the `SLAYERLOG_LOG_*` macros.

## Conventions

- Formatting is enforced by `.clang-format` (WebKit base, Allman braces, 4-space indent, 240 column limit, pointer binds to type, regrouped includes with `SortIncludes: false`). Run clang-format on changed files.
- Everything is in `namespace slayerlog`.
- Strong index types: `AllLineIndex`, `VisibleLineIndex`, `FindResultIndex` etc. (`log_types.hpp`) wrapped in `IndexedVector` — keep the distinction between an *entry* index (all lines) and a *visible* index (post-filter rows); conversions go through `AllProcessedSources`.
