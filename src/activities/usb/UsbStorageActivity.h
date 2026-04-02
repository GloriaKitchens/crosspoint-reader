#pragma once

#include "activities/Activity.h"

// USBMSC is only available on chips with USB OTG hardware (e.g. ESP32-S3).
// ESP32-C3 only has USB Serial/JTAG (SOC_USB_OTG_SUPPORTED is not defined),
// so we forward-declare here and guard the implementation in the .cpp.
class USBMSC;

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
  USBMSC* msc = nullptr;  // Heap-allocated in onEnter(); null on unsupported hardware
  bool mscStarted = false;
  bool notSupported = false;  // True when hardware lacks USB OTG; shows an error screen

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
