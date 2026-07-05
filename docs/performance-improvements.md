# Performance improvements

Findings from a dataflow performance review (2026-07-05) of the pipeline
watchers → tracked sources → merge (`AllTrackedSources::_all_lines`) → view model
(`AllProcessedSources`) → LogView pull render. The recurring themes: O(entire-log)
recomputations sitting in per-poll / per-frame paths (quadratic behaviour while
streaming), everything sharing one `model_mutex` with file I/O inside it, and heavy
per-line allocation.

Status: `done` = implemented + tested, `planned` = accepted but not yet done,
`not planned` = recorded only.

| # | Finding | Severity | Status |
|---|---------|----------|--------|
| 1 | `max_rendered_line_width()` re-renders every visible row, called per frame | Critical | done |
| 2 | `expand_visible_entries` does a full `rebuild_visible_entries()` on every append | Critical | done |
| 3 | Filter matching builds a per-entry haystack string even with no filters; `std::regex` unoptimized | Critical | done |
| 4 | Watcher thread holds `model_mutex` during file I/O / decompress / SSH drain | High | done |
| 5 | `SLAYERLOG_LOG_*` macros format eagerly; watcher hot path escapes whole chunks per poll | High | done |
| 6 | Every rebuild deep-copies every line; per-entry `source_label` string copy | High | done |
| 7 | `find_rewrite_start_index` scans the merged vector from the front (O(n) per out-of-order poll) | High | done |
| 8 | Find rebuild renders every row via `to_string` on query set / full-change notify | Medium | not planned |
| 9 | `render_log_entry_line` uses `ostringstream`; timestamp formatted twice per row | Medium | not planned |
| 10 | Selection decorations render the whole selected range per frame, not just visible rows | Medium | not planned |
| 11 | `StreamLineBuffer::append` copies each line twice; misc linear scans | Low | not planned |
| 12 | Initial open / zstd materialize the whole file as one allocation (transient 2× memory) | Medium | partially addressed by #4 |

## Details

### 1. Widest-line width recomputed by rendering the whole log every frame — done
`LogViewComponent::OnRender` → `AllProcessedSourcesLogViewData::widest_line_width()` →
`AllProcessedSources::max_rendered_line_width()` looped over all `_visible_rows` and built
each row's full display string (`std::ostringstream`) just to take `.size()`, per frame,
under `model_mutex`.

**Change:** `AllProcessedSources` now tracks the maximum *message* width of visible rows
incrementally (two buckets: with and without the extracted original timestamp, so the
`show-original-time` toggle stays O(1)), updated as rows become visible during
rebuild/expand and when a hidden-identical run grows. `max_rendered_line_width()` is now
O(1): global column padding + the active bucket, with the hidden-column range applied
formulaically (the erase length is monotone in line length, so the max is exact).

### 2. Every append rebuilt all visible rows — done
`append_lines_immediately` → `expand_visible_entries(first_new_entry_index)` ignored its
argument and called the full `rebuild_visible_entries()`: O(all entries) with 2+ string
allocations per entry, on every poll that delivered lines → O(n²) over a stream's
lifetime.

**Change:** `expand_visible_entries` now processes only the new entries. The only
cross-row state — the dedup text of the last filter-matching entry — is recovered from
the last visible row (an entry row or the last entry of a trailing hidden-identical run,
which by construction shares its dedup text). The hide-before cutoff is respected for
appended entries. Full rebuild still happens where it must (filter/dedup/hide changes,
replaces).

### 3. Filter matching allocation + regex cost — done
`entry_matches_active_filters` built `source_label + "\n" + presented_text(entry)` (a
full copy of the line text) per entry per rebuild, even when no filters were configured.
`re:` filters go through `std::regex`.

**Change:** short-circuit when both pattern lists are empty; searchable text is built
into a reusable scratch buffer (no fresh allocations per entry once warm); regexes
compile with `std::regex::optimize`. Supporting change: `TrackedSourceBase` now caches
its mnemonic prefix (recomputed only when the mnemonic or its visibility changes) and
`mnemonic_prefix()` / `presented_prefix()` return `const std::string&`, removing an
allocation per rendered/filtered row. Note: `std::regex` remains inherently slow — an
engine swap was judged out of scope; with fix 2 it no longer runs over the whole log per
poll, only over new lines.

### 4. Model mutex held during watcher I/O — done
The watcher thread locked `model_mutex`, then `poll()` performed file reads, zstd
decompression, SSH pipe drains, parsing and merging before unlocking. The render thread
needs the same mutex every frame: a slow read or a large burst froze the UI for its full
duration (and the initial read of a huge file arrived as one unbounded chunk).

**Change (bounded-ingest design, not an ownership restructure):**
- `FileWatcher` reads at most 4 MiB per poll and reports whether a backlog remains.
- `ZstdFileWatcher` decompresses incrementally (persistent stream state) with the same
  per-poll output cap, instead of one-shot whole-file decompression.
- `SshTailWatcher` caps the bytes drained per poll; OS pipe backpressure holds the rest.
- `LogWatcher` gained `backlog_pending()`; tracked sources and `AllTrackedSources::poll`
  surface it, and the watcher thread in `main.cpp` re-polls almost immediately (1 ms)
  while a backlog exists instead of sleeping the full poll interval. Catch-up throughput
  stays high (≈4 MiB per short cycle) while each lock hold stays ~a frame long.
- Opening a source drains the watcher to completion (loop) so open semantics are
  unchanged.

Residual (recorded, acceptable): the open path itself still runs under the command's
lock with a progress notification, as before; a full poll-outside-the-mutex ownership
restructure (shared_ptr sources + collect/ingest split) was considered and deliberately
deferred — the bounded-ingest design achieves the latency goal with far less risk.

### 5. Debug log macros formatted eagerly — done
`SLAYERLOG_LOG_*` built the complete message string unconditionally; log4cplus filtered
only afterwards. The file watcher poll path TRACE-logged `quote_for_log(chunk)` (escaping
the entire chunk char-by-char), TRACE-logged every parsed line, and DEBUG-logged every
line again via `describe_lines_for_log` — so ingest paid full formatting cost even with
those levels disabled.

**Change:** every macro now checks `debug_log::level_enabled(severity)` (→
`Logger::isEnabledFor`) before constructing the message. No call sites changed; disabled
levels now cost one level check.

### 6. Per-line deep copies and per-entry label strings — done
`merge_log_batch` cloned every entry (`make_shared<LogEntry>(entry)` — full text +
metadata copy, including its own `std::string source_label`) into `_all_lines`; folder
sources cloned an additional time (child → folder), so folder lines existed 3× and file
lines 2× in memory. Every open/close/offset/format change re-cloned everything
(`rebuild_all_lines`), which is also why align-time nudges were expensive.

**Change:**
- Top-level merge now *shares* entry pointers: `LogBatchSourceRange` merges stamp
  `metadata.source_index` on the source-owned entry instead of cloning
  (`MergeEntryMode::Share`); the folder-internal merge keeps cloning
  (`MergeEntryMode::Clone`) because folder entries need folder-level sequence/source
  stamps without disturbing child entries.
- `metadata.source_label` is no longer stamped per entry. Readers use the new
  `entry_source_label(entry)` helper: `metadata.source->source_label()` when the source
  pointer is set (always true for real entries), falling back to the stored field for
  manually built entries (tests, transient align entries).
- Effect: file-source lines exist once in memory (was 2×), folder lines twice (was 3×);
  `rebuild_all_lines` after an offset nudge is now a pointer re-merge with no string
  allocations; labels can never go stale.

Discovered while making this change (pre-existing, unchanged in behaviour): entries
retained by a *paused* `AllProcessedSources` keep their `metadata.source` pointer after
the source is closed, so rendering while paused after a close dereferences a destroyed
`TrackedSourceBase`. This was equally true before — the old clones copied the same raw
pointer — and the normal close path replaces the processed view under the same lock, so
it only bites in the pause+close window. Worth a follow-up (e.g. flush the pause on
close, or hold sources by `shared_ptr` in entry metadata).

### 7. Rewrite-point search scanned from the front — done
When new entries arrived older than the current tail (common with 2+ live sources with
skewed latencies), `find_rewrite_start_index` scanned `_all_lines` from index 0 → O(total
lines) per affected poll.

**Change:** scan backwards from the tail for the first entry with an effective timestamp
`< earliest_new`, tracking the earliest `>=` match seen; because timestamped entries are
ordered, the result is identical to the forward scan and the cost is O(rewritten suffix).

### 8. Find rebuild renders every row — not planned (this round)
`LogViewFindManager::rebuild_matches_from` calls `_data->to_string()` (full render) per
row for the whole view on a new query and after any full-change notification. Mitigated
indirectly by #9-style render cheapening if done later; a proper fix would search entry
text directly or cache rendered strings.

### 9. `render_log_entry_line` unit cost — not planned (this round)
`ostringstream` + `setw` per row; the timestamp is formatted once for width and again for
rendering. A `std::string`+`reserve` builder would cut the constant several-fold. This
unit cost is multiplied by #8 and by every visible row every frame.

### 10. Selection decorations cover the full selected range per frame — not planned
`LogViewSelection::decorations` iterates every line of the selection each frame. Should
clamp to the visible window that `draw_content` already receives.

### 11. Small stuff — not planned
`StreamLineBuffer::append` copies each SSH line twice (`std::string line =
_pending_fragment;` → should move); `visible_line_index_for_entry` linear scan though
`_visible_rows` is ordered by entry index; `go_to_next_match` linear `std::find` over a
sorted match list.

### 12. Whole-file materialization on open — partially addressed by #4
`read_file_tail` read the entire unread tail as one string; zstd decompressed the whole
archive into one string before splitting into lines (transient ~2× file size). The
per-poll caps from #4 bound both the allocation size and the lock hold for the streaming
path and for zstd; the very first read after `open` still ingests the full file (in
capped chunks, under the open command's progress notification). The zstd watcher still
reads the *compressed* bytes fully into memory up front (they are released once
decompression completes).

## Verification

All changes are covered by the unit suite: 279 tests pass (`ctest --preset
windows-clang-debug`), including new tests added with this work:

- `LogModelTest.MaxRenderedLineWidthMatchesRenderedRowsThroughModelChanges`,
  `…AppliesHiddenColumns`, `…CoversHiddenIdenticalRunRows` — pit the O(1) width formula
  against a brute-force render of every row across appends, filters, hide-before,
  hidden columns and the show-original-time toggle (fix 1).
- `LogModelTest.AppendingContinuesHiddenIdenticalRunsLikeAFullRebuild` and
  `LogModelTest.HideBeforeLineAppliesToLinesAppendedLater` — incremental expansion
  equivalence with the full rebuild, including dedup-run continuation across batches and
  a hide-before cutoff ahead of the current end (fix 2).
- `LogBatchTest.SharedRangeMergeStampsSourceIndexOntoSourceOwnedEntries` and
  `LogBatchTest.ClonedRangeMergeLeavesSourceOwnedEntriesUntouched` — the Share/Clone
  merge contract (fix 6).
- `FileWatcherTest.BoundedReadsSpreadTailOverPollsAndReportBacklog` and
  `ZstdFileWatcherTest.BoundedOutputSpreadsDecompressionOverPollsAndReportsBacklog` —
  capped ingest with backlog reporting and full-content reassembly across polls (fix 4).
