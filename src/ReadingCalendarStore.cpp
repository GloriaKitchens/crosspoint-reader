#include "ReadingCalendarStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <algorithm>
#include <ctime>

ReadingCalendarStore ReadingCalendarStore::instance;

uint32_t ReadingCalendarStore::encodeDate(int year, int month, int day) {
  return static_cast<uint32_t>(year) * 10000 + static_cast<uint32_t>(month) * 100 + static_cast<uint32_t>(day);
}

uint32_t ReadingCalendarStore::today() {
  time_t now = time(nullptr);
  // time() returns -1 if unavailable, and epoch (near 0) if clock not set.
  // We treat any time before year 2020 as not set and return 0.
  if (now < 0 || now < 1577836800L /* 2020-01-01 */) {
    return 0;
  }
  struct tm t;
  localtime_r(&now, &t);
  return encodeDate(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

void ReadingCalendarStore::recordPageRead() {
  const uint32_t date = today();
  if (date == 0) {
    // Clock not set; skip recording to avoid polluting data with epoch dates
    return;
  }

  auto it = std::find_if(records.begin(), records.end(), [date](const DayRecord& r) { return r.date == date; });
  if (it != records.end()) {
    if (it->pagesRead < UINT16_MAX) {
      it->pagesRead++;
    }
  } else {
    DayRecord rec{date, 1};
    // Insert in sorted order
    auto pos = std::lower_bound(records.begin(), records.end(), rec,
                                [](const DayRecord& a, const DayRecord& b) { return a.date < b.date; });
    records.insert(pos, rec);

    // Trim oldest entries if over the limit
    while (static_cast<int>(records.size()) > MAX_RECORDS) {
      records.erase(records.begin());
    }
  }

  dirty = true;
}

uint16_t ReadingCalendarStore::getPagesForDate(uint32_t date) const {
  auto it = std::find_if(records.begin(), records.end(), [date](const DayRecord& r) { return r.date == date; });
  return (it != records.end()) ? it->pagesRead : 0;
}

uint16_t ReadingCalendarStore::getMaxPagesForMonth(int year, int month) const {
  const uint32_t monthStart = encodeDate(year, month, 1);
  const uint32_t monthEnd = encodeDate(year, month, 31);
  uint16_t maxPages = 0;
  for (const auto& rec : records) {
    if (rec.date >= monthStart && rec.date <= monthEnd) {
      if (rec.pagesRead > maxPages) {
        maxPages = rec.pagesRead;
      }
    }
  }
  return maxPages;
}

bool ReadingCalendarStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  bool ok = JsonSettingsIO::saveCalendar(*this, "/.crosspoint/reading_calendar.json");
  if (ok) {
    dirty = false;
  }
  return ok;
}

bool ReadingCalendarStore::loadFromFile() {
  if (!Storage.exists("/.crosspoint/reading_calendar.json")) {
    return false;
  }
  String json = Storage.readFile("/.crosspoint/reading_calendar.json");
  if (json.isEmpty()) {
    return false;
  }
  return JsonSettingsIO::loadCalendar(*this, json.c_str());
}
