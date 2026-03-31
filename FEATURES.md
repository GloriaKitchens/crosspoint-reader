# CrossPoint Reader — Planned Features

This document describes features that are planned for future releases of CrossPoint Reader. Each section explains the intended behaviour, relevant design considerations for the ESP32-C3 hardware, and links to related code where applicable.

Contributions are welcome! If you want to work on one of these features, please open a Discussion first so we can coordinate effort and review scope alignment with [SCOPE.md](SCOPE.md).

---

## ~~1. Page Bookmarking~~ ✅ Implemented

~~**Summary:** Allow the reader to save one or more named bookmark positions within a book, and jump back to them from a bookmark list inside the reader menu.~~

~~**Intended behaviour:**~~
- ~~Press a designated button combination or use the reader menu to add a bookmark at the current page.~~
- ~~Bookmarks are displayed in a list accessible from the reader menu, showing the chapter name and page number.~~
- ~~Selecting a bookmark navigates directly to that page.~~
- ~~Bookmarks persist across power cycles (stored in the book's `.crosspoint/epub_<hash>/` cache directory).~~

~~**Design notes:**~~
- ~~Store bookmark data in a new `bookmarks.bin` file alongside `progress.bin` in the book's `.crosspoint/epub_<hash>/` cache directory (see [docs/file-formats.md](docs/file-formats.md)). Give the file its own version number so future format changes can be handled cleanly.~~
- ~~Keep the in-memory representation small: a fixed-size array of `struct Bookmark { uint16_t chapter; uint16_t page; }` avoids heap fragmentation.~~
- ~~The UI list must be orientation-aware and use the `GUI` macro for consistent theming.~~

---

## ~~2. Book Completion Tracking~~ ✅ Implemented

~~**Summary:** Record when a book is finished and display a "Finished" badge or filter on the library and home screens.~~

~~**Intended behaviour:**~~
- ~~When the reader reaches the last page of the last chapter, the firmware marks the book as finished.~~
- ~~The home screen and recent-books list display a visual indicator (e.g. a tick or "✓") next to finished books.~~
- ~~A "Finished" filter option is added to the Browse Files screen to show only completed books.~~
- ~~The finished state can be manually toggled from the reader menu or book details view.~~

~~**Design notes:**~~
- ~~Store the `finished` flag by incrementing the `progress.bin` format version and adding a `uint8_t finished` field (see [docs/file-formats.md](docs/file-formats.md) for the versioning protocol).~~
- ~~The `RecentBooksStore` (see `src/RecentBooksStore.h`) may need a `bool finished` field added to its persisted record.~~
- ~~Avoid scanning all cache directories on every home screen render — load state lazily when displaying each book entry.~~

---

## 3. Reading Time Tracking

**Summary:** Track the total time a user has spent reading each book and surface this in the book details view.

**Intended behaviour:**
- The firmware records cumulative reading time per book (time with the reader activity active and not on the sleep screen).
- A "Time spent reading" figure (e.g. "3 h 24 min") is shown in the reader menu or a dedicated book-info screen.
- An optional device-wide total ("You have read for X hours this week") can be surfaced on the home screen.

**Design notes:**
- Use `millis()` or a FreeRTOS tick count captured on `onEnter()` / `onExit()` of the reader activity to measure session duration.
- Persist accumulated time by incrementing the `progress.bin` format version and adding a `uint32_t readingTimeSeconds` field (see [docs/file-formats.md](docs/file-formats.md)). A 32-bit counter handles over 136 years of reading time.
- Debounce writes — update the file at most once per minute or on reader activity exit, not on every page turn (respect SPIFFS/SD write-cycle limits, see coding standards).
- Do **not** count time spent on the sleep screen or in menus.

---

## 4. Web Interface: Multi-File Upload

**Summary:** Extend the built-in web file manager to allow selecting and uploading multiple files in a single browser interaction.

**Current state:** The file manager already queues multiple uploads when the browser sends them sequentially (see [docs/webserver.md](docs/webserver.md)), but the HTML form `<input type="file">` element does not yet carry the `multiple` attribute, so most browsers present only a single-file picker.

**Intended behaviour:**
- The upload form accepts multiple files at once via `<input type="file" multiple>`.
- A progress indicator shows per-file and overall upload status.
- Files are written to the currently-browsed directory on the SD card.
- Error handling reports which individual files succeeded or failed without aborting the whole batch.

**Design notes:**
- The change is primarily in the embedded HTML/JS served by the web server (see `scripts/build_html.py` and the HTML source in `src/network/html/`).
- The server-side handler already processes one file per HTTP POST; verify that the browser-side JS sends files sequentially (one POST per file) rather than as a multipart batch, to avoid large in-memory buffers on the ESP32-C3.
- Test with both desktop Chrome and mobile Safari, which handle `multiple` differently.

---

## ~~5. Per-Book Progress Display~~ ✅ Implemented

~~**Summary:** Show reading progress (percentage read, estimated pages remaining) on the home screen, recent-books list, and browse-files screen.~~

~~**Intended behaviour:**~~
- ~~Each book entry on the home screen and recent-books list displays a small progress bar or percentage (e.g. "47%").~~
- ~~The browse-files screen shows progress for books that have been opened before.~~
- ~~Inside the reader, the existing page indicator is supplemented with an overall-book percentage.~~

~~**Design notes:**~~
- ~~Progress percentage = `(current_chapter_index * pages_per_chapter + current_page) / total_pages`.~~
- ~~Total page count is already available in cached `sections/*.bin` files after the first load; read it from the cache rather than re-parsing the EPUB.~~
- ~~For the list screens, load progress lazily (only for visible entries) to avoid reading dozens of cache files on every render.~~
- ~~The progress bar graphic must be drawn using the `GfxRenderer` primitives and respect the current orientation via `renderer.getOrientedViewableTRBL()`.~~

---

## ~~6. Reading Calendar~~ ✅ Implemented

~~**Summary:** Track how many pages are read each day and display them in a monthly calendar where cells are shaded proportionally — darker cells on days when more pages were read, lighter cells on days when fewer pages were read.~~

~~**Intended behaviour:**~~
- ~~Each forward page turn in the EPUB, TXT, and XTC readers increments a counter for the current calendar day.~~
- ~~A new **Reading Calendar** entry appears in the home screen menu (between Recent Books and File Transfer).~~
- ~~The calendar activity shows a month grid (7 columns × up to 6 rows).~~
  - ~~Days with zero reading are shown as empty white cells.~~
  - ~~Days with minimal reading are shown with a light-gray fill.~~
  - ~~Days with moderate reading are shown with a dark-gray fill.~~
  - ~~Days with the most reading (the daily maximum for the displayed month) are shown in black.~~
  - ~~The current day is indicated by a double border.~~
  - ~~Non-zero page counts are printed inside each cell.~~
- ~~The PageBack and PageForward side buttons navigate to the previous or next month.~~
- ~~The Back button returns to the home screen.~~
- ~~Reading history persists across power cycles via `/.crosspoint/reading_calendar.json`.~~

~~**Design notes:**~~
- ~~Shade levels are determined by dividing the day's page count by the maximum for the displayed month, then mapping to four discrete levels: white (0), LightGray (<34 %), DarkGray (<67 %), Black (≥67 %). Using the monthly maximum as the denominator rather than a fixed reference prevents well-read months from looking uniformly dark.~~
- ~~The data store (`ReadingCalendarStore`, see `src/ReadingCalendarStore.h`) holds at most 365 records, trimming the oldest entry whenever the limit is exceeded. Each record is 6 bytes (uint32_t YYYYMMDD date + uint16_t page count), keeping peak RAM usage well under 3 KB.~~
- ~~Page counts are only incremented on **forward** page turns to avoid inflating the count when the user re-reads passages.~~
- ~~The store is saved to disk on reader exit (`onExit()`), not on every page turn, to respect SD card write-cycle limits (see SPIFFS Write Throttling in the coding standards).~~
- ~~`time()` / `localtime_r()` are used to determine today's date. If the device clock is not set (i.e. `time()` returns a value before 2020-01-01), page turns are silently skipped so epoch-era records do not pollute the calendar.~~
- ~~The calendar layout is fully orientation-aware: cell sizes are derived from `renderer.getScreenWidth()` and `renderer.getScreenHeight()`, not hardcoded pixel values.~~

---

## 7. Reading Goals and Streaks

**Summary:** Let the user set a daily reading goal (e.g. 20 pages per day) and display a streak counter showing how many consecutive days the goal has been met.

**Intended behaviour:**
- A daily goal (in pages) is configurable from the Settings screen.
- The home screen shows a streak badge: "🔥 5-day streak" when the last N consecutive calendar days all met the goal.
- Missing a day resets the streak to zero.
- The current day's progress toward the goal is visible on the home screen (e.g. "12 / 20 pages today").

**Design notes:**
- Derive the streak from the existing `ReadingCalendarStore` records rather than storing it separately — this avoids additional persistence and keeps state consistent.
- Streak and daily progress should be recomputed lazily on `onEnter()` of the home activity, not on every page turn.
- The goal value is stored in `CrossPointSettings` as a `uint16_t dailyPageGoal`. A value of 0 means goals are disabled and the streak widget is hidden.

---

## 8. Table of Contents Jump

**Summary:** Add a quick-access table of contents (TOC) screen accessible from the reader menu so the user can jump to any chapter by name without scrolling through the chapter selection list.

**Intended behaviour:**
- A "Table of Contents" option appears in the EPUB reader menu.
- The TOC screen lists all chapter titles from the EPUB manifest in order, with the currently active chapter highlighted.
- Selecting a chapter navigates directly to the first page of that chapter.
- The list is scrollable using the Up/Down side buttons; Back cancels and returns to the reader.

**Design notes:**
- Chapter titles are already available via `Epub::getSpineTitle(index)` after the EPUB is loaded — no additional parsing is required.
- The activity should reuse `GUI.drawList()` for orientation-aware rendering, consistent with `EpubReaderChapterSelectionActivity`.
- Limit the displayed title length using `renderer.truncatedText()` to prevent overflow on narrow screens.

---

## 9. Night Mode / Inverted Display

**Summary:** Add a night-mode toggle that inverts the e-ink display (black background, white text) to reduce eye strain in low-light environments.

**Intended behaviour:**
- A "Night Mode" toggle is available in Settings and can also be toggled from the reader menu.
- When enabled, the screen renders with all pixels inverted: black background, white text, white UI chrome.
- The setting persists across power cycles.

**Design notes:**
- The `GfxRenderer` already supports pixel-level rendering; a post-render pass that inverts the framebuffer bit-by-bit is the simplest approach and avoids re-theming every component.
- The inversion should be applied immediately before `renderer.displayBuffer()` is called, in a single loop over `renderer.getFrameBuffer()`. At 48,000 bytes this takes under 1 ms on the 160 MHz RISC-V core.
- Store the setting as a `bool nightMode` in `CrossPointSettings`, defaulting to `false`.

---

## 10. Font Size Quick-Change

**Summary:** Allow the user to increase or decrease the reader font size from within the reader using a button combination, without opening the full settings menu.

**Intended behaviour:**
- A long-press of the Back button (or a configurable combo) cycles the font size through the available sizes for the current font family.
- The page is re-rendered immediately at the new size.
- The chosen size is saved to `CrossPointSettings` and persists across sessions.

**Design notes:**
- Available font sizes are already enumerated in `CrossPointSettings` (the existing font-size setting). Cycling through them is a matter of incrementing the index and wrapping.
- Re-rendering must call the normal `requestUpdate()` path to avoid partial redraws.
- The long-press threshold should respect `SETTINGS.longPressChapterSkip` so the behaviour remains consistent with other long-press actions.

---

## Contributing to These Features

1. Read [SCOPE.md](SCOPE.md) to confirm your feature aligns with the project mission.
2. Open a Discussion in the [ideas board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas) to coordinate with other contributors.
3. Follow the contribution guide in [docs/contributing/README.md](docs/contributing/README.md).
4. Reference the relevant section in this document in your PR description.

