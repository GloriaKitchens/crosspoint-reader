#include "SetDateTimeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <sys/time.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool SetDateTimeActivity::isLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int SetDateTimeActivity::daysInMonth(int y, int m) {
  static constexpr int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeapYear(y)) return 29;
  return days[m];
}

// Earliest timestamp we consider valid (2020-01-01 00:00:00 UTC)
static constexpr time_t EPOCH_THRESHOLD_2020 = 1577836800L;

void SetDateTimeActivity::onEnter() {
  Activity::onEnter();

  const time_t now = time(nullptr);
  if (now >= EPOCH_THRESHOLD_2020) {
    struct tm t;
    localtime_r(&now, &t);
    const int currentYear  = t.tm_year + 1900;
    const int currentMonth = t.tm_mon + 1;
    const int currentDay   = t.tm_mday;

    year   = std::clamp(currentYear, YEAR_MIN, YEAR_MAX);
    month  = currentMonth;
    day    = std::min(currentDay, daysInMonth(year, month));
    hour   = t.tm_hour;
    minute = t.tm_min;
  } else {
    year   = std::clamp(2024, YEAR_MIN, YEAR_MAX);
    month  = 1;
    day    = 1;
    hour   = 0;
    minute = 0;
  }

  selectedField = 0;
  requestUpdate();
}

void SetDateTimeActivity::onExit() {
  Activity::onExit();
}

void SetDateTimeActivity::adjustField(int delta) {
  switch (selectedField) {
    case 0:  // Year
      year += delta;
      if (year < YEAR_MIN) year = YEAR_MAX;
      else if (year > YEAR_MAX) year = YEAR_MIN;
      // Clamp day to valid range for the new year/month
      day = std::min(day, daysInMonth(year, month));
      break;
    case 1:  // Month
      month += delta;
      if (month < 1) month = 12;
      else if (month > 12) month = 1;
      day = std::min(day, daysInMonth(year, month));
      break;
    case 2:  // Day
      day += delta;
      if (day < 1) day = daysInMonth(year, month);
      else if (day > daysInMonth(year, month)) day = 1;
      break;
    case 3:  // Hour
      hour = (hour + delta + 24) % 24;
      break;
    case 4:  // Minute
      minute = (minute + delta + 60) % 60;
      break;
    default:
      break;
  }
  requestUpdate();
}

bool SetDateTimeActivity::saveDateTime() const {
  struct tm t = {};
  t.tm_year  = year - 1900;
  t.tm_mon   = month - 1;
  t.tm_mday  = day;
  t.tm_hour  = hour;
  t.tm_min   = minute;
  t.tm_sec   = 0;
  t.tm_isdst = -1;

  const time_t epochTime = mktime(&t);
  if (epochTime == static_cast<time_t>(-1)) {
    LOG_ERR("SetDT", "mktime failed for %04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return false;
  }

  struct timeval tv;
  tv.tv_sec  = epochTime;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    LOG_ERR("SetDT", "settimeofday failed (errno %d)", errno);
    return false;
  }

  LOG_INF("SetDT", "Date/time set: %04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
  return true;
}

void SetDateTimeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (saveDateTime()) {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    selectedField = (selectedField + FIELD_COUNT - 1) % FIELD_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    selectedField = (selectedField + 1) % FIELD_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    adjustField(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    adjustField(+1);
    return;
  }
}

void SetDateTimeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, tr(STR_SET_DATE_TIME));

  static const StrId fieldNames[FIELD_COUNT] = {
      StrId::STR_YEAR, StrId::STR_MONTH, StrId::STR_DAY, StrId::STR_HOUR, StrId::STR_MINUTE};

  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, screenW,
           screenH - (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight +
                      metrics.verticalSpacing * 2)},
      FIELD_COUNT, selectedField,
      [](int index) { return std::string(I18N.get(fieldNames[index])); },
      nullptr, nullptr,
      [this](int i) -> std::string {
        char buf[8];
        switch (i) {
          case 0: snprintf(buf, sizeof(buf), "%04d", year);   break;
          case 1: snprintf(buf, sizeof(buf), "%02d",  month);  break;
          case 2: snprintf(buf, sizeof(buf), "%02d",  day);    break;
          case 3: snprintf(buf, sizeof(buf), "%02d",  hour);   break;
          case 4: snprintf(buf, sizeof(buf), "%02d",  minute); break;
          default: buf[0] = '\0'; break;
        }
        return std::string(buf);
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  renderer.displayBuffer();
}
