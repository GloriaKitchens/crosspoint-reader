#pragma once
#include <cstdint>

// Maximum number of bookmarks stored per book.
// A fixed-size array avoids heap fragmentation on the ESP32-C3.
static constexpr int BOOKMARK_MAX_COUNT = 16;

// Version byte for bookmarks.bin format.
// Layout: [version(1B), count(1B), Bookmark[count] (4B each)] = 2 + count*4 bytes
static constexpr uint8_t BOOKMARKS_BIN_VERSION = 1;

struct Bookmark {
  uint16_t spineIndex;
  uint16_t page;
};

class BookmarkStore {
 public:
  // Load bookmarks from the book's cache directory. Returns true on success.
  // Clears any previously loaded bookmarks before loading.
  bool load(const char* cachePath);

  // Save bookmarks to the book's cache directory. Returns true on success.
  bool save(const char* cachePath) const;

  // Add a bookmark for the given spine/page. No-op if already present or full.
  // Returns true if the bookmark was added, false if it already exists or the
  // store is full.
  bool add(uint16_t spineIndex, uint16_t page);

  // Remove a bookmark matching the given spine/page. Returns true if removed.
  bool remove(uint16_t spineIndex, uint16_t page);

  // Returns true if a bookmark exists for the given spine/page.
  bool has(uint16_t spineIndex, uint16_t page) const;

  int count() const { return bookmarkCount; }

  const Bookmark& get(int index) const { return bookmarks[index]; }

 private:
  Bookmark bookmarks[BOOKMARK_MAX_COUNT] = {};
  int bookmarkCount = 0;
};
