#pragma once

#include <WebServer.h>

#include <memory>

/**
 * Minimal HTTP server for device-to-device sync (source device role).
 *
 * Serves the following endpoints:
 *   GET /sync/manifest  → JSON array of {path, size} for all syncable cache files
 *   GET /sync/file?path=<url-encoded-path>  → streams the requested file
 *
 * Syncable files are:
 *   /.crosspoint/reading_calendar.json  (calendar / pages-per-day data)
 *   /.crosspoint/state.json             (last open book and position)
 *   /.crosspoint/recent.json            (recent books list)
 *   /.crosspoint/epub_*/progress.bin    (per-book reading position)
 */
class DeviceSyncServer {
 public:
  static constexpr uint16_t PORT = 8765;
  static constexpr const char* SYNC_AP_SSID = "CrossPoint-Sync";

  DeviceSyncServer();
  ~DeviceSyncServer();

  void begin();
  void stop();
  void handleClient();
  bool isRunning() const { return running; }

 private:
  std::unique_ptr<WebServer> server;
  bool running = false;

  void handleManifest();
  void handleFile();
};
