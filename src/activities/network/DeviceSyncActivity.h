#pragma once

#include <DNSServer.h>

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/DeviceSyncServer.h"
#include "util/ButtonNavigator.h"

/**
 * DeviceSyncActivity - synchronise reading data between two Xteink X4 devices
 * over a direct WiFi connection.
 *
 * Role selection:
 *   Source device  → Creates a WiFi access point ("CrossPoint-Sync") and runs a
 *                    local HTTP server that exposes the .crosspoint cache files.
 *   Target device  → Connects to the source access point via WifiSelectionActivity,
 *                    then downloads all cache files (calendar, state, per-book
 *                    progress) from the source.
 *
 * Files synchronised:
 *   /.crosspoint/reading_calendar.json   – calendar / pages-per-day data
 *   /.crosspoint/state.json              – last open book and position
 *   /.crosspoint/recent.json             – recent books list
 *   /.crosspoint/epub_* /progress.bin    – per-book reading progress
 */
class DeviceSyncActivity final : public Activity {
 public:
  explicit DeviceSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeviceSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State {
    ROLE_SELECTION,
    AP_STARTING,
    SERVER_RUNNING,
    WIFI_SELECTION,
    FETCHING_MANIFEST,
    DOWNLOADING,
    DONE,
    FAILED,
  };

  State state = State::ROLE_SELECTION;
  int selectedRole = 0;  // 0 = Source, 1 = Target

  // Source mode
  std::unique_ptr<DeviceSyncServer> syncServer;
  std::unique_ptr<DNSServer> syncDns;
  std::string sourceIP;

  // Target mode
  std::string statusMessage;
  int filesTotal = 0;
  int filesDownloaded = 0;

  ButtonNavigator buttonNavigator;

  void startSourceMode();
  void startTargetMode();
  void onWifiSelectionComplete(bool connected);
  void performDownload(const std::string& sourceBaseUrl);
};
