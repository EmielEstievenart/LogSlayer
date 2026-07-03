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

Application code lives under `code/LogSlayer/`, split into **three CMake targets** so the core stays portable across UI toolkits (the split exists to make a future port to e.g. Qt straightforward):

- **`code/LogSlayer/core/`** → `logslayer_core` (static lib). The UI-framework-free heart: watchers, tracked/processed sources, sorting/merging, filtering, searching (`search_pattern`, `log_view_find_manager`), timestamps, settings, clipboard, command-line parsing, the notification *interface*, the command *model* (`CoreCommand`, `CommandManager`, `CommandContext`, `command_palette_model`, `command_history`, and the ~20 pure command actions under `commands/implementations/`), plus the `LogViewService` abstraction and the `AlignTimeSession` alignment model. **Links no FTXUI** — any `<ftxui/...>` or `<ftxui_components/...>` include in a core file is a hard compile error, which is what enforces portability.
- **`code/LogSlayer/ui/`** → `logslayer_ui` (static lib; depends on `logslayer_core` + FTXUI). The terminal UI: the LogView render stack (`LogView/`, including the dual-pane `align_time_controller`), the command palette controller/view + widgets, the interactive (picker) commands, the command registrar, the toast sink, the colour theme (`view_theme`), and `LogViewBridge` (the FTXUI implementation of the core `LogViewService`). The legacy LogView1 stack has been deleted; `docs/logview1-vs-logview2.md` records the migration.
- **`code/LogSlayer/app/`** → `LogSlayer` (executable). Just `main.cpp`: the composition root that wires core + ui together.

Everything under `libs/` is a vendored submodule dependency (ftxui, ftxui_components, googletest, log4cplus, zstd, nlohmann_json, simpleini, eestv) — do not edit those. `eestv` (`libs/eestv`) is the author's own utility library (`eestv_lib` target). Tests live under `tests/unit_tests/` and **link `logslayer_core` + `logslayer_ui`** (they no longer recompile the sources).

Each library's `CMakeLists.txt` collects its sources with `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so **adding a source file under the right tier directory is picked up automatically** — there is no source list to maintain. Put a new file under `core/` only if it has zero FTXUI dependency (verify by building the `logslayer_core` target alone); otherwise it belongs in `ui/`. The dependency direction is structural and one-way: **core → (nothing), ui → core, app → ui**.

## Architecture

The core is a polling pipeline from raw log sources to rendered terminal text, with a mutex-guarded model and a background watcher thread. `main.cpp` wires it all together.

**Data pipeline (the central abstraction layers):**

1. **Watchers** (`watchers/`) — produce raw lines from a transport. `LogWatcher`/`LogWatcherBase` interface; implementations include `file_watcher`, `zstd_file_watcher`, `ssh_tail_watcher` (over `process_pipe` + `stream_line_buffer`). Created via `log_watcher_factory`. Watchers are thread-safe (base class holds the mutex).

2. **Tracked sources** (`tracked_sources/`) — own watchers and turn raw lines into parsed `LogEntry` objects (timestamp + message + source/sequence metadata). `TrackedSourceBase` is the abstract base; `tracked_source_file`/`tracked_source_folder` are concrete; `tracked_source_factory` builds them. `AllTrackedSources` aggregates all sources, merges their entries into one time-ordered `IndexedVector` (`all_lines()`), and exposes `poll()` which returns the index of the first new/changed line. Owns timestamp formats and per-source timestamp offset/format.

3. **Processed sources** (`tracked_sources/all_processed_sources.hpp`) — `AllProcessedSources` is the view model. It takes entries from `AllTrackedSources` and applies all user-facing transforms: include/exclude filters, column hiding, identical-line deduplication, hide-before-line, pause, source labels, original-vs-aligned time. It distinguishes **all entries** from **visible rows** and renders entries to display strings. Append vs. replace paths matter: `append_from_sources` for streaming growth, `replace_from_sources` when earlier lines changed.

4. **The view renderer** — `LogViewComponent` (`ui/LogView/`) renders `AllProcessedSources` with a thin **pull-based** strategy: a `draw_content` callback pulls `to_string(row)` per visible row straight onto an FTXUI `Canvas` (via ftxui_components' `TextViewComponent`), recomputing from the model each frame rather than mirroring it into a buffer. It owns text selection/clipboard and its own `LogViewFindManager`; commands never touch it directly — they go through the core `LogViewService` interface, which `LogViewBridge` (ui) implements over the component + the FTXUI screen (refresh = redraw request; navigation drives the `TextViewController`).

**The log view.** `main.cpp` shows a single fullscreen `LogViewComponent` (`ui/LogView/`) backed by an `AllProcessedSourcesLogViewData` adapter over the one `AllProcessedSources` model, with one command palette. `LogViewData` is a small read-only interface (`size`/`to_string`/`widest_line_width`/`lock`/callbacks); the concrete adapter is what makes the read/write boundary explicit (the view can't mutate the model). LogView is the sole view: all commands are registered for it via `register_log_view_commands`, and it owns find, the dual-pane time-alignment mode, and the bottom status bar. (`docs/logview1-vs-logview2.md` is the historical record of the LogView1→LogView2 migration; LogView2 was then renamed to plain LogView.)

**Threading.** `main.cpp` spawns one background watcher thread (`start_watcher_thread`) that sleeps `poll_interval_ms`, calls `tracked_sources.poll()`, and on new data updates `AllProcessedSources` (append/replace) and posts a custom FTXUI event to redraw (the pull-based view re-renders from the model). A single `model_mutex` guards the whole model (tracked sources, processed sources); the view reads it through `LogViewData::lock`. **Any code reading or mutating the model must hold `model_mutex`** — commands receive it via `CommandContext::model_mutex`. The TSan preset exists to catch violations.

**Commands / command palette** (`commands/`) — the user-facing action system (Ctrl+P palette). The interface is split across the core/ui boundary: `CoreCommand` (core, `commands/core_command.hpp`) is the UI-agnostic contract (`descriptor`/`execute`/`has_active_interaction`/`cancel`/`hidden_column_preview`); `Command : public CoreCommand` (ui, `commands/command.hpp`) adds the FTXUI bits (`handle_event`/`render`/`render_help`). Pure actions (filter, find, open, hide, go-to-line, …) derive from `CoreCommand` and live in `core/commands/implementations/`; the interactive picker commands (including `align-time-view`) derive from `Command` and live in `ui/commands/implementations/`. Both take the same FTXUI-free `CommandContext` (processed sources, the core `LogViewService`, tracked sources, notifier, model mutex, background tasks, settings path — **no screen, no header_text**; view refreshes go through `LogViewService::rebuild_view`/`reload`). `CommandManager` (core) stores `CoreCommand`s; the palette controller (ui) `dynamic_cast`s the active command to `Command` to render it. The single registrar is **`register_log_view_commands`** (`command_registrar`, ui), which wires the shared command set (`register_shared_view_commands`) plus the LogView-specific open/align/find commands into the view's manager. `CommandPaletteController`/`CommandPaletteModel`/`CommandPaletteView` drive the palette UI; `command_history` persists recent commands. **Adding a feature = add a command implementation under the matching tier dir (a `CoreCommand` in `core/` if it needs no FTXUI, otherwise a `Command` in `ui/`) and register it in `register_log_view_commands` (`command_registrar.cpp`)** — the GLOB build picks the file up automatically.

**Settings** (`settings_store`, `settings_ini`) — INI-backed (simpleini) persistence for settings, command history, and timestamp formats. Platform-specific path resolved by `default_settings_file_path()` (LOCALAPPDATA / Application Support / XDG_CONFIG_HOME, with a CWD fallback). Settings loading is resilient: failures disable saves for the run rather than aborting.

**Timestamps** (`timestamp/`) — `TimestampFormatCatalog` holds the list of recognized formats (seeded from settings or `default_timestamp_formats()`); `SourceTimestampParser` parses per source; per-source offsets enable cross-source time alignment. The dual-pane alignment workflow's state machine lives in the core `AlignTimeSession`; the ui `AlignTimeController` (`ui/LogView/align_time_controller`) wraps it with the two FTXUI panes + event takeover, and `align_time_view_command` (ui) starts it via `LogViewService::begin_time_alignment`.

**Notifications** (`notifications/`) — `Notifier`/notification sink abstraction; the FTXUI implementation (`ftxui_toast_notification_sink`) renders toasts via ftxui_components' `ToastHostComponent`. The sink contract lives in `notification.hpp`: sinks must be thread-safe, treat `timeout <= 0` as sticky, and must not extend UI lifetime (the FTXUI sink holds the toast host weakly so `Notifier` copies in model objects cannot keep the UI tree alive). Auto-dismiss of completed progress notifications is core-side policy applied by `Notifier`, not sink behavior; use `Notifier::info/success/warning/error` for one-shot toasts, `NotificationHandle` + `make_progress_notification` for long-running operations.

`debug_log` (log4cplus, configured by `log4cplus.ini` copied next to the executable post-build) is a developer diagnostic log written to a temp path — separate from the logs the app *views*. Use the `SLAYERLOG_LOG_*` macros.

## Conventions

- Formatting is enforced by `.clang-format` (WebKit base, Allman braces, 4-space indent, 240 column limit, pointer binds to type, regrouped includes with `SortIncludes: false`). Run clang-format on changed files.
- Everything is in `namespace slayerlog`.
- Strong index types: `AllLineIndex`, `VisibleLineIndex`, `FindResultIndex` etc. (`log_types.hpp`) wrapped in `IndexedVector` — keep the distinction between an *entry* index (all lines) and a *visible* index (post-filter rows); conversions go through `AllProcessedSources`.
