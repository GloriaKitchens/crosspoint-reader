#include "BookmarkStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

static constexpr const char* MODULE = "BMK";

bool BookmarkStore::load(const char* cachePath) {
  bookmarkCount = 0;

  std::string path = std::string(cachePath) + "/bookmarks.bin";
  FsFile f;
  if (!Storage.openFileForRead(MODULE, path, f)) {
    // No file yet — start with empty bookmark list
    return true;
  }

  // Header: version(1B) + count(1B)
  uint8_t header[2];
  if (f.read(header, 2) != 2 || header[0] != BOOKMARKS_BIN_VERSION) {
    LOG_DBG(MODULE, "Unknown bookmarks.bin version or short read");
    f.close();
    return false;
  }

  const int storedCount = header[1];
  if (storedCount > BOOKMARK_MAX_COUNT) {
    LOG_ERR(MODULE, "Invalid bookmark count %d in file", storedCount);
    f.close();
    return false;
  }

  // Read each bookmark: spineIndex(2B LE) + page(2B LE)
  for (int i = 0; i < storedCount; i++) {
    uint8_t entry[4];
    if (f.read(entry, 4) != 4) {
      LOG_ERR(MODULE, "Short read at bookmark %d, treating file as corrupt", i);
      bookmarkCount = 0;
      f.close();
      return false;
    }
    bookmarks[bookmarkCount].spineIndex = static_cast<uint16_t>(entry[0] | (entry[1] << 8));
    bookmarks[bookmarkCount].page = static_cast<uint16_t>(entry[2] | (entry[3] << 8));
    bookmarkCount++;
  }

  f.close();
  LOG_DBG(MODULE, "Loaded %d bookmark(s)", bookmarkCount);
  return true;
}

bool BookmarkStore::save(const char* cachePath) const {
  std::string path = std::string(cachePath) + "/bookmarks.bin";
  FsFile f;
  if (!Storage.openFileForWrite(MODULE, path, f)) {
    LOG_ERR(MODULE, "Could not open bookmarks.bin for write");
    return false;
  }

  // Header: version(1B) + count(1B)
  uint8_t header[2];
  header[0] = BOOKMARKS_BIN_VERSION;
  header[1] = static_cast<uint8_t>(bookmarkCount);
  f.write(header, 2);

  // Entries: spineIndex(2B LE) + page(2B LE)
  for (int i = 0; i < bookmarkCount; i++) {
    uint8_t entry[4];
    entry[0] = bookmarks[i].spineIndex & 0xFF;
    entry[1] = (bookmarks[i].spineIndex >> 8) & 0xFF;
    entry[2] = bookmarks[i].page & 0xFF;
    entry[3] = (bookmarks[i].page >> 8) & 0xFF;
    f.write(entry, 4);
  }

  f.close();
  LOG_DBG(MODULE, "Saved %d bookmark(s)", bookmarkCount);
  return true;
}

bool BookmarkStore::add(uint16_t spineIndex, uint16_t page) {
  if (has(spineIndex, page)) {
    return false;
  }
  if (bookmarkCount >= BOOKMARK_MAX_COUNT) {
    LOG_DBG(MODULE, "Bookmark store full (%d)", BOOKMARK_MAX_COUNT);
    return false;
  }
  bookmarks[bookmarkCount].spineIndex = spineIndex;
  bookmarks[bookmarkCount].page = page;
  bookmarkCount++;
  return true;
}

bool BookmarkStore::remove(uint16_t spineIndex, uint16_t page) {
  for (int i = 0; i < bookmarkCount; i++) {
    if (bookmarks[i].spineIndex == spineIndex && bookmarks[i].page == page) {
      // Shift remaining entries down
      for (int j = i; j < bookmarkCount - 1; j++) {
        bookmarks[j] = bookmarks[j + 1];
      }
      bookmarkCount--;
      return true;
    }
  }
  return false;
}

bool BookmarkStore::has(uint16_t spineIndex, uint16_t page) const {
  for (int i = 0; i < bookmarkCount; i++) {
    if (bookmarks[i].spineIndex == spineIndex && bookmarks[i].page == page) {
      return true;
    }
  }
  return false;
}
