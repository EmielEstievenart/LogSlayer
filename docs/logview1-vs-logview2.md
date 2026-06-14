# LogView1 vs LogView2 — feature comparison & path to parity

> Status snapshot as of the `core/ui/app` tier split (commit `46aa797`).
> Goal: delete **LogView1** and bring **LogView2** to feature parity as the single log view.

## TL;DR

The gap is **large but lopsided**. It is almost entirely a *missing-command / missing-write-path* gap, **not** a rendering gap.

- LogView2's command palette registers **2 commands** (`open`, `find`); LogView1 registers **~25**. That single number captures most of the delta.
- LogView2's **rendering and interaction layer is already ~80% there**: canvas rendering with horizontal clipping, full keyboard + wheel scrolling, mouse click-drag selection + multi-line clipboard copy, incremental regex find with wrap-around next/prev + auto-center + highlight, focus-aware framing, a dedicated per-view palette, and thread-safe locking — all done.
- The big missing buckets, in descending effort: **(1)** the time-alignment workflow (XL), **(2)** all filtering/transform commands (L), **(3)** UI chrome (status bars, header, empty-state — M), **(4)** a handful of cheap navigation/util commands and key bindings (S).

**Net assessment:** deleting LV1 and bringing LV2 to parity is *feasible and architecturally sound* — LV2 is the better foundation — but it is a substantial effort dominated by re-wiring the ~20 commands behind a (better-designed) controlled write path and service facade. The renderer is mostly done.

---

## Architecture: why LV2 is the better brother

| | **LogView1** | **LogView2** |
|---|---|---|
| View component | `LogViewComponent` (`ui/LogView/`) | `LogView2Component` (`ui/LogView2/`) |
| Render strategy | **Double-buffered** rendered-text store (`_buffer_a`/`_buffer_b`), `rebuild_view` (full) vs `sync_view` (incremental append), replace-detection + column-width-growth signal | **Pull-based**: `draw_content` callback pulls `to_string(row)` straight onto an FTXUI `Canvas` per visible row. No second buffer, no sync-vs-rebuild surface. |
| Data access | Reads `AllProcessedSources` directly | Reads through a tiny abstract `LogView2Data` interface (read-only: `lock / size / widest_line_width / to_string / callbacks`) |
| Find | core `FindState` | own `LogView2FindManager` (core, over the `LogView2Data` interface) |
| Service facade | core `LogViewService` ⇐ `LogViewBridge` (`ui`) over `LogController` | **none** |
| Time alignment | core `TimeAlignmentModel` ⇐ `time_alignment_controller` (`ui`) | **none** |
| State held | Fat, stateful `LogController` mirrors the model into buffers that must be kept in sync | Minimal; recomputes from the source of truth on demand |

**Why LV2 is considered better-architected:** it inverts the dependency from a fat stateful renderer toward a minimal pull-based adapter. Rendering is recomputed from the source of truth instead of mirrored into a second buffer (eliminating the entire append/replace/column-width-growth synchronization surface and a class of buffer-staleness bugs). The read/write boundary is explicit — the view *physically cannot* mutate the model through `LogView2Data`. The core/ui split is cleaner (a tiny data interface vs. LV1's larger `LogViewService` surface).

**The catch (and the crux of the effort estimate):** that same thin read-only adapter is exactly what makes the *transform* features non-trivial to port. Every transform (filters, columns, dedup, hide-before, pause, show/hide original time) lives **inside `AllProcessedSources`**, which is read-write. To drive those from LV2, LV2 must grow a controlled write path and a `LogViewService`-equivalent facade — just designed better than LV1's.

### The single most important nuance

Today, in `app/main.cpp`, the two views run **side by side over the same model**:

```
main.cpp:342  AllProcessedSourcesLogView2Data(processed_sources, model_mutex)
```

LV2 is wired to an adapter over **the same `AllProcessedSources` instance LV1 owns**. Consequences:

- LV2 **passively reflects LV1's state today**: toggle a filter, dedup, hidden columns, or original-time from LV1's palette and LV2's output changes too, because `to_string → rendered_line → render_entry` reads the same model.
- This is why several "missing" features are really *"LV2 has no command of its own"* rather than *"LV2 is blind to it."* (Verification flagged **show/hide original time** and **original-vs-aligned timestamp** as `partial`, not `missing`, for exactly this reason.)
- **Once LV1 is deleted, this dilemma dissolves.** There is one view and one `AllProcessedSources` — LV2 simply *owns* it. The "shared vs. its own model" question the analysis kept raising goes away: LV2 drives the only model there is.

This reframes the work: it is **not** "port 20 commands from scratch." The ~20 core command actions are *already UI-agnostic* (they operate on `AllProcessedSources` + the `LogViewService` interface via `CommandContext`). What's actually missing is **a `LogViewService` implementation backed by LV2**, after which most commands re-register unchanged.

---

## Feature parity matrix

Legend — **status**: `parity` (equivalent) · `partial` (weaker in LV2) · `missing` (LV1-only) · `lv2-only`.
**effort** = rough cost to close the gap in LV2 (S/M/L/XL). All gap claims below were adversarially verified against the code.

### Sources
| Feature | Status | LV2 today / gap | Effort |
|---|---|---|---|
| Open file (local + `ssh://`) | **parity** | `open` dispatches to LV1's `OpenFileCommand` | S |
| Open folder | **parity** | `open` stats path, dispatches to `OpenFolderCommand` | S |
| Close source | **missing** | no close command; needs an interactive picker + LV2 refresh path | M |

### Filtering / transforms
| Feature | Status | LV2 today / gap | Effort |
|---|---|---|---|
| Include filter | **missing** | needs a write path + command (`add_include_filter` exists in core) | L |
| Exclude filter | **missing** | same | L |
| Reset / clear filters | **missing** | register once filter model is drivable | S |
| Delete individual filters | **missing** | port the multi-select picker `Command` | M |
| Hide columns | **missing** | register + add a preview overlay path | M |
| Hidden-column preview highlight | **missing** | add preview overlay to `draw_content` (find coloring already proves the mechanism) | M |
| Reset / clear column filter | **missing** | register both | S |
| Dedup identical lines (show/hide) | **missing** | register toggle (intrinsic to `AllProcessedSources`) | M |
| Hide before line | **missing** | register command | S |
| Hide shown lines | **missing** | register + a first/last-visible-line query for LV2 | M |
| Pause / resume streaming | **missing** | `p` key + PAUSED badge; `toggle_pause` already in core | M |

### Search
| Feature | Status | LV2 today / gap | Effort |
|---|---|---|---|
| Find / search | **partial** | works via `LogView2FindManager`; reports **total-only** match count (LV1 shows visible-vs-total); is a *separate* search impl from core `FindState` | S |
| Find next/prev + viewport centring | **parity** | Right/Left navigate, wrap, `center_on_line` | S |
| Find highlight + active-match emphasis | **parity** | `draw_content` colors matches | S |
| Clear find / Esc semantics | **parity** | Esc clears active find | S |
| Selection-seeded find (Ctrl+F prefill) | **parity** | Ctrl+F pre-fills `find <selection>` | S |
| Find inside time-alignment mode | **missing** | only relevant once alignment is ported | S |

### Navigation
| Feature | Status | LV2 today / gap | Effort |
|---|---|---|---|
| Go to line | **missing** | needs a center-on-line facade + command (TextViewController already has `center_on_line`) | S |
| Vertical scrolling | **partial** | all keys/wheel done; **missing `k`/`j` aliases** | S |
| Horizontal scrolling (+ fast step) | **parity** | Left/Right + Ctrl+Left/Right | S |
| Follow-bottom (auto-scroll) | **parity** | inherited from `TextViewController` | S |

### Selection / clipboard
| Feature | Status | LV2 today | Effort |
|---|---|---|---|
| Text selection (mouse drag) | **parity** | `LogView2Selection`, clamped/normalized/inverted | S |
| Clipboard copy (multi-line) | **parity** | `C` + right-click | S |
| Mouse hit-testing (x/y → position) | **parity** | `text_position_at` | S |

### Timestamps
| Feature | Status | LV2 today / gap | Effort |
|---|---|---|---|
| **Time alignment (cross-source)** | **missing** | **largest single gap**: needs a `TimeAlignmentModel` facade for LV2, in-view event takeover (Enter/PageUp-Down/Ctrl+F/Esc), selected-line highlight, ALIGN badge, command | **XL** |
| Adjust time offset | **missing** | port the `TextInputPanel` picker `Command` + LV2 reload path | M |
| Clear time offset | **missing** | port the picker `Command` + reload path | S |
| Set time format | **missing** | port the two-step picker `Command` + reload path | M |
| Show / hide original time | **missing**¹ | register both commands (¹LV2 already *reflects* the setting via the shared model — it just lacks its own command) | S |
| Original-vs-aligned timestamp column | **partial** | LV2 passively shows the effective/aligned timestamp; no control of its own | L |

### Rendering / chrome
| Feature | Status | LV2 today / gap | Effort |
|---|---|---|---|
| Streaming incremental render | **partial** | LV2's on-demand pull redraw is simpler and renders correctly, but lacks LV1's append optimization; assess scaling for very large logs | M |
| Line numbers / timestamp gutter / labels | **parity** | baked in upstream by `AllProcessedSources` | S |
| Window / header + focus framing | **partial** | framing matches; PAUSED badge **done** (in the window title); **no source-label header line** | S |
| Status bars (filter / find / align / key-hints) | **parity¹** | filter, find, key-hints rows **done** (`LogView2Component::OnRender`, read via new read-only `LogView2Data` status accessors). ¹align row intentionally omitted — alignment is a full-screen takeover (`AlignTimeController::render`) so the main view is never on screen then; find shows `N matches` (visible==total in LV2). Long filter rows truncate (same as LV1, uncapped). | M |
| Empty-state messaging | **missing** | no `<empty file>` / `<no matching lines>` placeholder | S |
| Reset / clear view + reload after source change | **partial** | reflects passively via shared model; **no reload facade of its own** | M |

### Misc / infrastructure
| Feature | Status | LV2 today / gap | Effort |
|---|---|---|---|
| Command history (Ctrl+R) | **lv2-only** | LV2 also adds **Ctrl+O** = history pre-filtered to `open` | — |
| Per-view command palette | **partial** | infra at parity; gap is the command **set** (2 vs ~25) | L |
| Commands surfaced in `--help` | **missing** | LV2's manager is never passed to `build_help_text` | S |
| Thread-safe model access (mutex) | **parity** | all reads go through `LogView2Data::lock` | S |
| **UI-agnostic service facade** (`LogViewService`) | **missing** | **the keystone**: only `LogViewBridge` over LV1 exists; LV2 has none, which is *why* only 2 commands are wired | L |
| Quit / exit handling | **missing** | no `q`/Esc-to-exit; LV2 can't quit the app on its own | S |

---

## What this means for the port

Two cross-cutting prerequisites unlock most of the rest:

1. **A `LogViewService`-equivalent facade for LV2 (the keystone).** The existing core command actions call `context.log_view.rebuild_view()/reload()/go_to_line()/center/start_time_alignment()`. Implement that interface over LV2's renderer (mostly "invalidate + post redraw", far simpler than LV1's double-buffer) and ~20 commands drop in **unchanged**.
2. **LV2 owns the model.** After LV1 is deleted, point LV2's `LogView2Data` at the (now sole) `AllProcessedSources` and let LV2's commands drive it. No "shared vs own" dilemma remains.

After those, the genuinely net-new engineering is:
- **Time alignment in LV2** (XL): an in-view alignment controller (the core `TimeAlignmentModel` is reusable) + ALIGN badge + event takeover + the `start_time_alignment` service hook.
- **Find reconciliation** (S/M): decide between core `FindState` and `LogView2FindManager`. The latter is cleaner and already integrated; adopting it means dropping `FindState`/`find_command` and teaching the find badge a visible-vs-total count (which equals total until/unless LV2 grows its own filter layer — which it won't, since it owns the one model).
- **Chrome** (M): four status rows, source-label header line, PAUSED badge, empty-state placeholder.
- **Small tail** (S): `k`/`j` aliases, go-to-line, export-visible-text, copy-settings-path, `--help` inclusion, `q`/Esc quit.

The ~5 interactive (`Command`-derived) pickers currently under `ui/commands/implementations/log_view1/` (adjust-time-offset, clear-time-offset, close-source, delete-filters, set-time-format) are **not actually LV1-specific in logic** — they operate on shared `tracked_sources`/`processed_sources`/`log_view` via `CommandContext`. "Porting" them is mostly re-registration + a LV2 reload path; they (and the `log_view1/` core actions) should be moved out of the `log_view1/` directory once LV1 is gone.

## Recommended sequencing

Deleting LV1 *before* LV2 reaches parity leaves the app feature-regressed in between. Safer order:

1. ✅ **Write down the differences** (this document).
2. **Build the LV2 keystone**: a `LogView2`-backed `LogViewService` (`LogView2Bridge`) + point LV2 at the model directly.
3. **Re-register the cheap core commands** behind it (filters, dedup, columns, hide-before, go-to-line, export, copy-settings-path, show/hide original time) and verify each lands in LV2.
4. **Add chrome** (status bars, header, PAUSED badge, empty-state) and the small key/quit/`--help` items.
5. **Port the interactive pickers** (close-source, delete-filters, adjust/clear offset, set format) + reload path.
6. **Port time alignment** (the XL item).
7. **Delete LogView1** (`ui/LogView/`, `LogViewComponent`/`LogController`/`LogViewBridge`, core `FindState` if dropped, the `time_alignment_controller`/LV1 plumbing) and flatten the `log_view1`/`log_view2` command directory split.

(If you'd rather delete LV1 first and rebuild — accepting a temporary regression — steps 2–6 still apply, just with LV1 gone from `main.cpp` from the start.)
