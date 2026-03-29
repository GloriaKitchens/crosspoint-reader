#include "UsbStorageActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <USB.h>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// Raw-block callbacks for TinyUSB MSC.
// These must be static functions (not lambdas with captures) to avoid
// heap-allocated closures and to satisfy the function-pointer requirement.
// ---------------------------------------------------------------------------

static int32_t usbMscOnRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (bufsize % 512 != 0) {
    LOG_ERR("USB_MSC", "Read: bufsize %u is not a multiple of 512", bufsize);
    return -1;
  }
  const uint32_t count = bufsize / 512;
  const uint32_t startLba = lba + (offset / 512);
  if (!Storage.readBlocks(startLba, static_cast<uint8_t*>(buffer), count)) {
    LOG_ERR("USB_MSC", "Read failed: lba=%u count=%u", startLba, count);
    return -1;
  }
  LOG_DBG("USB_MSC", "Read: lba=%u count=%u", startLba, count);
  return static_cast<int32_t>(bufsize);
}

static int32_t usbMscOnWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (bufsize % 512 != 0) {
    LOG_ERR("USB_MSC", "Write: bufsize %u is not a multiple of 512", bufsize);
    return -1;
  }
  const uint32_t count = bufsize / 512;
  const uint32_t startLba = lba + (offset / 512);
  if (!Storage.writeBlocks(startLba, buffer, count)) {
    LOG_ERR("USB_MSC", "Write failed: lba=%u count=%u", startLba, count);
    return -1;
  }
  LOG_DBG("USB_MSC", "Write: lba=%u count=%u", startLba, count);
  return static_cast<int32_t>(bufsize);
}

// ---------------------------------------------------------------------------

void UsbStorageActivity::onEnter() {
  Activity::onEnter();

  LOG_INF("USB_MSC", "Entering USB storage mode");

  // Get sector count BEFORE unmounting so the filesystem geometry is known.
  const uint32_t sectors = Storage.sectorCount();
  if (sectors == 0) {
    LOG_ERR("USB_MSC", "sectorCount() returned 0 — cannot start USB MSC");
    activityManager.goHome();
    return;
  }
  LOG_INF("USB_MSC", "SD card sector count: %u", sectors);

  // Unmount the filesystem so the host can have exclusive raw block access.
  Storage.end();
  LOG_INF("USB_MSC", "SD card unmounted");

  // Configure the MSC device identity.
  msc.vendorID("Xteink");
  msc.productID("X4 SD Card");
  msc.productRevision("1.0");

  // Register raw-block callbacks.
  msc.onRead(usbMscOnRead);
  msc.onWrite(usbMscOnWrite);

  // Advertise the media as present and writable.
  msc.mediaPresent(true);

  if (msc.begin(sectors, 512)) {
    mscStarted = true;
    LOG_INF("USB_MSC", "USB MSC started: %u blocks x 512 bytes", sectors);
  } else {
    LOG_ERR("USB_MSC", "USB MSC begin() failed — remounting SD card");
    if (!Storage.begin()) {
      LOG_ERR("USB_MSC", "SD card remount also failed after MSC begin() error");
    }
    activityManager.goHome();
    return;
  }

  requestUpdate();
}

void UsbStorageActivity::onExit() {
  if (mscStarted) {
    msc.end();
    mscStarted = false;
    LOG_INF("USB_MSC", "USB MSC stopped");
  }

  // Remount the filesystem so the rest of the firmware can use it again.
  if (!Storage.begin()) {
    LOG_ERR("USB_MSC", "Failed to remount SD card after USB storage session");
  } else {
    LOG_INF("USB_MSC", "SD card remounted");
  }

  Activity::onExit();
}

void UsbStorageActivity::loop() {
  // Back button → user requested disconnect.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    LOG_INF("USB_MSC", "Back pressed — exiting USB storage mode");
    exitUsbStorage();
    return;
  }

  // USB cable unplugged → exit automatically.
  if (!gpio.isUsbConnected()) {
    LOG_INF("USB_MSC", "USB disconnected — exiting USB storage mode");
    exitUsbStorage();
  }
}

void UsbStorageActivity::exitUsbStorage() { activityManager.goHome(); }

void UsbStorageActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_USB_STORAGE));

  // Centre the description text vertically in the content area.
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textTop = contentTop + (contentBottom - contentTop - lineHeight) / 2;

  renderer.drawCenteredText(UI_10_FONT_ID, textTop, tr(STR_USB_STORAGE_DESC));

  // Draw the Back = disconnect button hint.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
