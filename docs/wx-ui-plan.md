# WxWidgets UI plan

LogSlayer gains a second, WxWidgets-based UI alongside the FTXUI terminal UI.
This document records the locked design decisions and the milestone plan; it is
the reference for all wx-UI work. Status of individual milestones is tracked in
the checklist at the bottom.

## Locked decisions

| Topic | Decision |
|---|---|
| Target scope | Full LogView2 feature parity before the wx UI becomes the default. |
| Landing strategy | Incremental commits on `main`. The wx UI is selectable via `--ui gui` from its first commit; FTXUI stays the default until parity; the final parity commit flips the default to the GUI. |
| Binary layout | One console-subsystem `LogSlayer` executable hosting both UIs. In GUI mode the process calls `FreeConsole()` (Windows) after argument parsing so a console window opened by Explorer goes away; `--help` and CLI errors always print to the normal console. |
| UI selection | `--ui <gui|tui>`, a validated enum option in the core command-line parser (unknown value → clean error, same pattern as `--poll-interval-ms`). Default is `tui` until parity, then `gui`. |
| Interaction model | Palette-centric, shared with the TUI: the same Ctrl+P command palette, commands, and history in both UIs. The palette/matching/history/selection behavior is extracted from the FTXUI controller into core so the UIs cannot drift. The wx UI additionally gets a menu bar whose items drive the same commands. |
| Interactive pickers | Stay palette interactions. Their interaction state (choice lists, multi-select, two-stage flows, text input with preview) moves into core view-model classes (the pattern `TimeAlignmentModel`/`AlignTimeSession` already follow); each UI renders those models with its own widgets. |
| Log view widget | Custom-drawn wx control (`wxWindow` subclass, paint handler pulls visible rows from `LogView2Data` under the model mutex) — architecturally identical to the FTXUI canvas view, so selection/find-highlight/follow semantics port 1:1 and rendering stays O(viewport). Not `wxStyledTextCtrl` (push-based document mirror), not virtual `wxListCtrl` (row-level selection only). |
| Folder / target | `code/LogSlayer/ui_wx/` → static lib `logslayer_ui_wx`. Dependency rule: `ui_wx → core` only, never `ui_wx → ui`. App links both UI libs and dispatches at runtime. |
| wxWidgets vendoring | Git submodule `libs/wxwidgets` (https URL), pinned to v3.3.2 (native Windows dark mode, current-toolchain support; the pin controls API drift — verified building clean under `windows-clang-debug`, no fallback needed). Static wx (`wxBUILD_SHARED=OFF`), dynamic CRT (repo default), samples/tests/demos off — matching the established submodule pattern in the root CMakeLists. The whole wx tier is gated by `LOGSLAYER_ENABLE_WX_UI` (default ON Windows/macOS, OFF Linux where wx needs GTK3/X11 dev packages); when off, a `run_gui` stub reports the build has no GUI. |
| Refactor sequencing | "Phase 0" seam extraction happens before any wx code, as small no-behavior-change commits with the FTXUI app re-wired onto each new seam and tests green throughout. |
| Settings | Both UIs share the same settings file, command history, and timestamp-format storage. |
| Tests | New core extractions get unit tests in `unit_tests` (which keeps linking `logslayer_core + logslayer_ui`). No wx widget tests initially; `ui_wx` is not linked into the test binary until it has something core-testable. |

## Why Phase 0 (what leaks today)

The core seam (`LogViewService`, `NotificationSink`, `LogView2Data`,
`CommandContext`, core find/align state machines) is already framework-free,
but FTXUI leaks past it in these places, all of which the wx UI needs:

1. **No redraw abstraction** — the watcher thread, `reload_processed_sources`,
   `LogView2Bridge`, and `AlignTimeController` post `ftxui::Event::Custom` to a
   concrete `ScreenInteractive&`.
2. **Watcher loop lives in `main.cpp`** — poll / replace-vs-append / width-growth
   logic would otherwise be duplicated per composition root.
3. **`--help` constructs the full FTXUI stack** (screen included) just to
   enumerate command descriptors.
4. **CLI parse errors escape `main()` uncaught** → `std::terminate` instead of a
   clean nonzero exit.
5. **Palette behavior sits in the FTXUI `CommandPaletteController`** (matching,
   history mode, selection, hidden-column preview, picker flows) while only
   `CommandPaletteModel` is core.
6. **Six interactive picker commands are FTXUI widgets** (`SingleSelectList`,
   `MultiSelectList`, `TextInputPanel`, …).
7. **`header_text` is vestigial** — rebuilt on every reload, threaded through
   `LogView2Bridge`, rendered by nothing that still compiles. It gets removed,
   not ported.

## Milestones

- **M0a — CLI**: `--ui <gui|tui>` in `Config` (default `tui`); catch
  `po::error` in `main` → exit 2. Parser tests.
- **M0b — Help**: core `NullLogViewService`; help path registers commands and
  prints before any FTXUI object exists.
- **M0c — Redraw + watcher seams**: core `RedrawScheduler`
  (`request_redraw()`, thread-safe); FTXUI impl posts `Event::Custom`; watcher
  loop + reload move to core; `header_text` removed; `LogView2Bridge` and
  `AlignTimeController` lose their `ScreenInteractive&` where it was only used
  to post redraws.
- **M1 — wx skeleton**: submodule + CMake + `ui_wx` lib; `main.cpp` becomes a
  thin dispatcher over `run_tui(...)` / `run_gui(...)`; minimal wx frame with
  the custom-drawn log view (scroll, follow-tail, quit), `WxRedrawScheduler`,
  shared core watcher thread. Build verified on `windows-clang-debug` (fallbacks
  per the vendoring decision).
- **M2 — palette core extraction**: palette session state machine to core;
  FTXUI controller re-wired onto it; behavior unchanged in the TUI.
- **M3 — wx palette + commands**: wx palette window over the shared session;
  all pure core commands work from the wx palette; command history shared.
- **M4 — picker view-models**: the 6 interactive commands' interaction state to
  core; FTXUI widgets re-wired; wx picker rendering.
- **M5 — wx view parity**: find highlighting + navigation, text selection +
  clipboard, status bar, toasts (wx `NotificationSink`), theme (semantic colors
  from `view_theme` mapped to `wxColour`; dark mode via wx 3.3 on Windows).
- **M6 — wx align-time**: dual-pane align mode over core `AlignTimeSession`.
- **M7 — menus + polish**: wx menu bar driving the same commands; keyboard
  parity audit against `docs/logview1-vs-logview2.md` conventions.
- **M8 — default flip**: `--ui` default becomes `gui`; docs + CLAUDE.md updated;
  parity checklist verified.

## Status

- [x] Grilling interview: decisions above locked (2026-07-02)
- [x] M0a — `--ui <gui|tui>` + clean CLI error exit (2026-07-02)
- [x] M0b — `--help` decoupled from FTXUI via core `NullLogViewService` (2026-07-02)
- [x] M0c — core `RedrawScheduler` + watcher loop/`model_refresh` in core; `header_text` removed (2026-07-02)
- [x] M1 — wxWidgets v3.3.2 submodule (builds clean under `windows-clang-debug`, no fallback needed); `ui_wx` skeleton (`WxLogView`/`WxMainFrame`/`WxRedrawScheduler`); `main.cpp` dispatches to `run_tui`/`run_gui`; GUI verified live: renders, tails appends, closes clean (2026-07-02)
- [ ] M2
- [ ] M3
- [ ] M4
- [ ] M5
- [ ] M6
- [ ] M7
- [ ] M8
