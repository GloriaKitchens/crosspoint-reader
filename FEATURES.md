# CrossPoint Reader — Planned Features

This document describes features that are planned for future releases of CrossPoint Reader. Each section explains the intended behaviour, relevant design considerations for the ESP32-C3 hardware, and links to related code where applicable.

Contributions are welcome! If you want to work on one of these features, please open a Discussion first so we can coordinate effort and review scope alignment with [SCOPE.md](SCOPE.md).

---

## 1. Page Bookmarking

**Summary:** Allow the reader to save one or more named bookmark positions within a book, and jump back to them from a bookmark list inside the reader menu.

**Intended behaviour:**
- Press a designated button combination or use the reader menu to add a bookmark at the current page.
- Bookmarks are displayed in a list accessible from the reader menu, showing the chapter name and page number.
- Selecting a bookmark navigates directly to that page.
- Bookmarks persist across power cycles (stored in the book's `.crosspoint/epub_<hash>/` cache directory).

**Design notes:**
- Store bookmark data in a new `bookmarks.bin` file alongside `progress.bin` in the book's `.crosspoint/epub_<hash>/` cache directory (see [docs/file-formats.md](docs/file-formats.md)). Give the file its own version number so future format changes can be handled cleanly.
- Keep the in-memory representation small: a fixed-size array of `struct Bookmark { uint16_t chapter; uint16_t page; }` avoids heap fragmentation.
- The UI list must be orientation-aware and use the `GUI` macro for consistent theming.

---

## 2. Book Completion Tracking

**Summary:** Record when a book is finished and display a "Finished" badge or filter on the library and home screens.

**Intended behaviour:**
- When the reader reaches the last page of the last chapter, the firmware marks the book as finished.
- The home screen and recent-books list display a visual indicator (e.g. a tick or "✓") next to finished books.
- A "Finished" filter option is added to the Browse Files screen to show only completed books.
- The finished state can be manually toggled from the reader menu or book details view.

**Design notes:**
- Store the `finished` flag by incrementing the `progress.bin` format version and adding a `uint8_t finished` field (see [docs/file-formats.md](docs/file-formats.md) for the versioning protocol).
- The `RecentBooksStore` (see `src/RecentBooksStore.h`) may need a `bool finished` field added to its persisted record.
- Avoid scanning all cache directories on every home screen render — load state lazily when displaying each book entry.

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

## 5. Per-Book Progress Display

**Summary:** Show reading progress (percentage read, estimated pages remaining) on the home screen, recent-books list, and browse-files screen.

**Intended behaviour:**
- Each book entry on the home screen and recent-books list displays a small progress bar or percentage (e.g. "47%").
- The browse-files screen shows progress for books that have been opened before.
- Inside the reader, the existing page indicator is supplemented with an overall-book percentage.

**Design notes:**
- Progress percentage = `(current_chapter_index * pages_per_chapter + current_page) / total_pages`.
- Total page count is already available in cached `sections/*.bin` files after the first load; read it from the cache rather than re-parsing the EPUB.
- For the list screens, load progress lazily (only for visible entries) to avoid reading dozens of cache files on every render.
- The progress bar graphic must be drawn using the `GfxRenderer` primitives and respect the current orientation via `renderer.getOrientedViewableTRBL()`.

---

## Contributing to These Features

1. Read [SCOPE.md](SCOPE.md) to confirm your feature aligns with the project mission.
2. Open a Discussion in the [ideas board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas) to coordinate with other contributors.
3. Follow the contribution guide in [docs/contributing/README.md](docs/contributing/README.md).
4. Reference the relevant section in this document in your PR description.
