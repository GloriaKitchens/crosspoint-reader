#pragma once

#include <USBMSC.h>

#include "activities/Activity.h"

/**
 * UsbStorageActivity
 *
 * Presents a holding screen while the SD card is exposed to the host computer
 * as a USB Mass Storage Class (MSC) device.
 *
 * Lifecycle:
 *   onEnter() — saves app state, unmounts the SD card, and starts USB MSC.
 *   loop()    — monitors USB connection and Back button; exits on disconnect.
 *   onExit()  — stops USB MSC and remounts the SD card.
 *   render()  — draws a simple holding screen with button hints.
 */
class UsbStorageActivity final : public Activity {
  USBMSC msc;
  bool mscStarted = false;

  void exitUsbStorage();

 public:
  explicit UsbStorageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbStorage", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return true; }
};
