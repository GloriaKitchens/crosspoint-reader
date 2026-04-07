#include "DeviceSyncServer.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

namespace {
// Candidate fixed files to include in the sync manifest.
constexpr const char* SYNC_FIXED_FILES[] = {
    "/.crosspoint/reading_calendar.json",
    "/.crosspoint/state.json",
    "/.crosspoint/recent.json",
};
constexpr size_t SYNC_FIXED_FILES_COUNT = sizeof(SYNC_FIXED_FILES) / sizeof(SYNC_FIXED_FILES[0]);

// Root directory of device cache
constexpr const char* CROSSPOINT_DIR = "/.crosspoint";
}  // namespace

DeviceSyncServer::DeviceSyncServer() {}

DeviceSyncServer::~DeviceSyncServer() { stop(); }

void DeviceSyncServer::begin() {
  server.reset(new WebServer(PORT));

  server->on("/sync/manifest", HTTP_GET, [this] { handleManifest(); });
  server->on("/sync/file", HTTP_GET, [this] { handleFile(); });
  server->onNotFound([this] { server->send(404, "text/plain", "Not found"); });

  server->begin();
  running = true;
  LOG_DBG("DSS", "DeviceSyncServer started on port %d", PORT);
}

void DeviceSyncServer::stop() {
  if (server) {
    server->stop();
    server.reset();
  }
  running = false;
  LOG_DBG("DSS", "DeviceSyncServer stopped");
}

void DeviceSyncServer::handleClient() {
  if (server) {
    server->handleClient();
  }
}

void DeviceSyncServer::handleManifest() {
  LOG_DBG("DSS", "handleManifest");

  // Stream the manifest as a JSON array to avoid building it all in memory.
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  bool first = true;

  // Fixed candidate files
  for (size_t i = 0; i < SYNC_FIXED_FILES_COUNT; i++) {
    const char* path = SYNC_FIXED_FILES[i];
    if (!Storage.exists(path)) {
      continue;
    }
    HalFile f = Storage.open(path);
    if (!f || f.isDirectory()) {
      continue;
    }
    const size_t sz = f.fileSize();
    f.close();

    // Build JSON entry: {"path":"...","size":N}
    char entry[192];
    snprintf(entry, sizeof(entry), "%s{\"path\":\"%s\",\"size\":%zu}", first ? "" : ",", path, sz);
    server->sendContent(entry);
    first = false;
  }

  // Per-book progress files: enumerate epub_* subdirectories
  {
    char dirName[128] = {0};
    HalFile dir = Storage.open(CROSSPOINT_DIR);
    if (dir && dir.isDirectory()) {
      for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        if (entry.isDirectory()) {
          entry.getName(dirName, sizeof(dirName));
          // Only process epub_* directories
          if (strncmp(dirName, "epub_", 5) == 0) {
            entry.close();

            // Build progress.bin path
            char progressPath[128];
            snprintf(progressPath, sizeof(progressPath), "%s/%s/progress.bin", CROSSPOINT_DIR, dirName);
            if (Storage.exists(progressPath)) {
              HalFile pf = Storage.open(progressPath);
              if (pf && !pf.isDirectory()) {
                const size_t sz = pf.fileSize();
                pf.close();

                char jsonEntry[192];
                snprintf(jsonEntry, sizeof(jsonEntry), "%s{\"path\":\"%s\",\"size\":%zu}",
                         first ? "" : ",", progressPath, sz);
                server->sendContent(jsonEntry);
                first = false;
              }
            }
          } else {
            entry.close();
          }
        } else {
          entry.close();
        }
        esp_task_wdt_reset();
      }
      dir.close();
    }
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("DSS", "Manifest sent");
}

void DeviceSyncServer::handleFile() {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  // Copy the path arg into a fixed-size stack buffer to avoid Arduino String
  // heap allocation (String Policy, Resource Protocol rule 4).
  char pathBuf[256] = {0};
  const String& argPath = server->arg("path");
  if (argPath.length() == 0 || argPath.length() >= sizeof(pathBuf)) {
    server->send(400, "text/plain", "Invalid path length");
    return;
  }
  argPath.toCharArray(pathBuf, sizeof(pathBuf));
  const char* reqPath = pathBuf;

  // Security: only serve files under /.crosspoint/
  if (strncmp(reqPath, "/.crosspoint/", 13) != 0) {
    server->send(403, "text/plain", "Forbidden");
    return;
  }

  // Security: prevent directory traversal
  if (strstr(reqPath, "..") != nullptr) {
    server->send(403, "text/plain", "Forbidden");
    return;
  }

  if (!Storage.exists(reqPath)) {
    server->send(404, "text/plain", "Not found");
    return;
  }

  HalFile file = Storage.open(reqPath);
  if (!file || file.isDirectory()) {
    server->send(404, "text/plain", "Not found");
    return;
  }

  const size_t fileSize = file.fileSize();

  server->setContentLength(fileSize);
  server->send(200, "application/octet-stream", "");

  NetworkClient client = server->client();
  // Use a 512-byte stack buffer (conserves stack space on ESP32-C3)
  uint8_t buf[512];
  bool ok = true;
  while (ok && file.available()) {
    const int result = file.read(buf, sizeof(buf));
    if (result <= 0) break;
    size_t totalWritten = 0;
    const size_t bytesRead = static_cast<size_t>(result);
    while (totalWritten < bytesRead) {
      esp_task_wdt_reset();
      const size_t wrote = client.write(buf + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        ok = false;
        break;
      }
      totalWritten += wrote;
    }
  }

  file.close();
  LOG_DBG("DSS", "Served file: %s (%zu bytes)", reqPath, fileSize);
}
