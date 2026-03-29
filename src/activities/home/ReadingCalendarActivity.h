#pragma once

#include <cstdint>

#include "../Activity.h"

class ReadingCalendarActivity final : public Activity {
  int viewYear = 0;
  int viewMonth = 0;  // 1-12

  void renderCalendar();
  static void currentYearMonth(int* outYear, int* outMonth);
  void navigateMonth(int delta);

 public:
  explicit ReadingCalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingCalendar", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
