#include "ReadingCalendarActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "ReadingCalendarStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Returns true if a given year is a leap year
bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Returns the number of days in a given month (month is 1-12)
int daysInMonth(int year, int month) {
  static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return days[month];
}

// Returns the day-of-week for the 1st of the given month (0=Sun, 6=Sat)
int firstDayOfWeek(int year, int month) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = 1;
  mktime(&t);
  return t.tm_wday;
}

// Abbreviated month names via i18n
const char* monthName(int month) {
  static const StrId ids[] = {
      StrId::STR_MONTH_JANUARY, StrId::STR_MONTH_FEBRUARY, StrId::STR_MONTH_MARCH,
      StrId::STR_MONTH_APRIL,   StrId::STR_MONTH_MAY,      StrId::STR_MONTH_JUNE,
      StrId::STR_MONTH_JULY,    StrId::STR_MONTH_AUGUST,   StrId::STR_MONTH_SEPTEMBER,
      StrId::STR_MONTH_OCTOBER, StrId::STR_MONTH_NOVEMBER, StrId::STR_MONTH_DECEMBER};
  if (month < 1 || month > 12) return "";
  return I18N.get(ids[month - 1]);
}

// Day-of-week header abbreviations via i18n (Sunday first)
const char* dayHeader(int dow) {
  static const StrId ids[] = {StrId::STR_DAY_SUN, StrId::STR_DAY_MON, StrId::STR_DAY_TUE, StrId::STR_DAY_WED,
                               StrId::STR_DAY_THU, StrId::STR_DAY_FRI, StrId::STR_DAY_SAT};
  if (dow < 0 || dow > 6) return "";
  return I18N.get(ids[dow]);
}

// Map a ratio (0.0-1.0) to a dither Color for fill
// 0 → white (no fill), low → LightGray, mid → DarkGray, high → Black
Color ratioToColor(float ratio) {
  if (ratio <= 0.0f) {
    return White;
  }
  if (ratio < 0.34f) {
    return LightGray;
  }
  if (ratio < 0.67f) {
    return DarkGray;
  }
  return Black;
}

}  // namespace

void ReadingCalendarActivity::currentYearMonth(int* outYear, int* outMonth) {
  time_t now = time(nullptr);
  if (now < 1577836800L /* 2020-01-01 */) {
    // Clock not set; fall back to a fixed reference date
    *outYear = 2024;
    *outMonth = 1;
    return;
  }
  struct tm t;
  localtime_r(&now, &t);
  *outYear = t.tm_year + 1900;
  *outMonth = t.tm_mon + 1;
}

void ReadingCalendarActivity::navigateMonth(int delta) {
  viewMonth += delta;
  if (viewMonth > 12) {
    viewMonth = 1;
    viewYear++;
  } else if (viewMonth < 1) {
    viewMonth = 12;
    viewYear--;
  }
  requestUpdate();
}

void ReadingCalendarActivity::onEnter() {
  Activity::onEnter();
  currentYearMonth(&viewYear, &viewMonth);
  requestUpdate();
}

void ReadingCalendarActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    navigateMonth(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    navigateMonth(1);
    return;
  }
}

void ReadingCalendarActivity::render(RenderLock&&) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  renderer.clearScreen();

  // ---- Header ----
  char headerBuf[32];
  snprintf(headerBuf, sizeof(headerBuf), "%s %d", monthName(viewMonth), viewYear);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, headerBuf);

  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  const int hintsHeight = metrics.buttonHintsHeight;
  const int calendarAreaH = screenH - headerBottom - hintsHeight;

  // ---- Layout ----
  // 7 columns (days of week), rows for weeks
  const int cellW = screenW / 7;
  const int dayHeaderH = 24;
  const int gridH = calendarAreaH - dayHeaderH;
  const int cellH = gridH / 6;  // Reserve space for up to 6 week rows

  const uint16_t maxPages = READING_CALENDAR.getMaxPagesForMonth(viewYear, viewMonth);

  // ---- Day-of-week headers ----
  const int dayHeaderY = headerBottom + 4;
  for (int d = 0; d < 7; d++) {
    const int x = d * cellW + cellW / 2;
    const char* const hdr = dayHeader(d);
    const int w = renderer.getTextWidth(UI_10_FONT_ID, hdr);
    renderer.drawText(UI_10_FONT_ID, x - w / 2, dayHeaderY, hdr, true);
  }

  // Separator line under day headers
  const int gridTop = dayHeaderY + dayHeaderH;
  renderer.drawLine(0, gridTop - 2, screenW, gridTop - 2, false);

  // ---- Calendar grid ----
  const int numDays = daysInMonth(viewYear, viewMonth);
  const int startDow = firstDayOfWeek(viewYear, viewMonth);  // 0=Sun

  // Today's encoded date for highlighting
  const uint32_t todayDate = ReadingCalendarStore::today();
  const uint32_t thisMonthYear = static_cast<uint32_t>(viewYear) * 100 + static_cast<uint32_t>(viewMonth);

  int col = startDow;
  int row = 0;

  for (int day = 1; day <= numDays; day++) {
    const int cellX = col * cellW;
    const int cellY = gridTop + row * cellH;

    const uint32_t date = ReadingCalendarStore::encodeDate(viewYear, viewMonth, day);
    const uint16_t pages = READING_CALENDAR.getPagesForDate(date);

    // Fill cell with shade based on reading intensity
    const int padding = 2;
    const int fillX = cellX + padding;
    const int fillY = cellY + padding;
    const int fillW = cellW - padding * 2;
    const int fillH = cellH - padding * 2;

    if (fillW > 0 && fillH > 0) {
      if (maxPages > 0 && pages > 0) {
        const float ratio = static_cast<float>(pages) / static_cast<float>(maxPages);
        const Color shade = ratioToColor(ratio);
        renderer.fillRectDither(fillX, fillY, fillW, fillH, shade);
      }
      renderer.drawRect(cellX, cellY, cellW, cellH, false);
    }

    // Day number
    char dayBuf[4];
    snprintf(dayBuf, sizeof(dayBuf), "%d", day);
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, dayBuf);
    const int textX = cellX + cellW / 2 - textW / 2;
    const int textY = cellY + 3;

    // Draw day number in inverted colour when cell is darkly filled (DarkGray or Black)
    const bool invertText = (maxPages > 0 && pages > 0 &&
                             (static_cast<float>(pages) / static_cast<float>(maxPages)) >= 0.67f);
    renderer.drawText(UI_10_FONT_ID, textX, textY, dayBuf, !invertText);

    // Highlight today's date with a border
    if (date == todayDate) {
      renderer.drawRect(cellX + 1, cellY + 1, cellW - 2, cellH - 2, true);
    }

    // Pages count below day number (only if non-zero and cell is large enough)
    if (pages > 0 && cellH >= 30) {
      char pagesBuf[8];
      snprintf(pagesBuf, sizeof(pagesBuf), "%d", static_cast<int>(pages));
      const int pw = renderer.getTextWidth(UI_10_FONT_ID, pagesBuf);
      renderer.drawText(UI_10_FONT_ID, cellX + cellW / 2 - pw / 2, cellY + cellH - 14, pagesBuf, !invertText);
    }

    col++;
    if (col == 7) {
      col = 0;
      row++;
    }
  }

  // ---- Empty data message if no records at all ----
  if (READING_CALENDAR.getRecords().empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, screenH / 2, tr(STR_NO_READING_DATA), true);
  }

  // ---- Button hints ----
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_PREV_MONTH), tr(STR_NEXT_MONTH));

  renderer.displayBuffer();
}
