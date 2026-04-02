#pragma once

#include "../Activity.h"

class SetDateTimeActivity final : public Activity {
  static constexpr int FIELD_COUNT = 5;
  static constexpr int YEAR_MIN = 2020;
  static constexpr int YEAR_MAX = 2099;

  int year = 2024;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;

  int selectedField = 0;

  void adjustField(int delta);
  void saveDateTime() const;

  static bool isLeapYear(int year);
  static int daysInMonth(int year, int month);

 public:
  explicit SetDateTimeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SetDateTime", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
