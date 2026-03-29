#pragma once

#include <cstdint>
#include <vector>

struct DayRecord {
  uint32_t date;      // YYYYMMDD encoded as year*10000 + month*100 + day
  uint16_t pagesRead;
};

class ReadingCalendarStore;
namespace JsonSettingsIO {
bool loadCalendar(ReadingCalendarStore& store, const char* json);
}  // namespace JsonSettingsIO

class ReadingCalendarStore {
  static ReadingCalendarStore instance;

  std::vector<DayRecord> records;  // Sorted by date ascending
  mutable bool dirty = false;

  static constexpr int MAX_RECORDS = 365;

  friend bool JsonSettingsIO::loadCalendar(ReadingCalendarStore&, const char*);

 public:
  static ReadingCalendarStore& getInstance() { return instance; }

  // Disable copy
  ReadingCalendarStore(const ReadingCalendarStore&) = delete;
  ReadingCalendarStore& operator=(const ReadingCalendarStore&) = delete;

  // Encode date as YYYYMMDD from year, month (1-12), day (1-31)
  static uint32_t encodeDate(int year, int month, int day);

  // Get today's date encoded as YYYYMMDD, or 0 if clock is not available
  static uint32_t today();

  // Increment pages read for today by 1
  void recordPageRead();

  // Get pages read for a specific date (0 if no entry)
  uint16_t getPagesForDate(uint32_t date) const;

  // Get the maximum pages read in any single day within a given month (for display normalisation)
  uint16_t getMaxPagesForMonth(int year, int month) const;

  const std::vector<DayRecord>& getRecords() const { return records; }

  bool saveToFile() const;
  bool loadFromFile();

 private:
  ReadingCalendarStore() = default;
};

#define READING_CALENDAR ReadingCalendarStore::getInstance()
