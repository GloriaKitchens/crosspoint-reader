#include "DeviceSyncActivity.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "WifiSelectionActivity.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
constexpr const char* SYNC_AP_SSID = DeviceSyncServer::SYNC_AP_SSID;
constexpr uint8_t SYNC_AP_CHANNEL = 6;
constexpr uint8_t SYNC_AP_MAX_CONN = 1;
// Number of sync-server requests processed per main loop iteration.
// Balances responsiveness against watchdog-timer budget.
constexpr int SERVER_REQUESTS_PER_LOOP = 64;
constexpr int ROLE_COUNT = 2;
constexpr uint16_t DNS_PORT = 53;
}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void DeviceSyncActivity::onEnter() {
  Activity::onEnter();
  state = State::ROLE_SELECTION;
  selectedRole = 0;
  filesTotal = 0;
  filesDownloaded = 0;
  statusMessage.clear();
  sourceIP.clear();
  requestUpdate();
}

void DeviceSyncActivity::onExit() {
  Activity::onExit();

  // Stop sync server (source mode)
  if (syncServer) {
    syncServer->stop();
    syncServer.reset();
  }

  // Tear down DNS helper
  if (syncDns) {
    syncDns->stop();
    syncDns.reset();
  }

  // Disconnect / power down WiFi
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false);
  delay(30);  // Allow disconnect frame to be sent before powering off radio
  WiFi.mode(WIFI_OFF);
  delay(30);  // Allow WiFi hardware to complete power-down sequence
}

// ---------------------------------------------------------------------------
// Source mode
// ---------------------------------------------------------------------------

void DeviceSyncActivity::startSourceMode() {
  {
    RenderLock lock(*this);
    state = State::AP_STARTING;
  }
  requestUpdate(true);

  WiFi.mode(WIFI_AP);
  delay(100);

  const bool started = WiFi.softAP(SYNC_AP_SSID, nullptr, SYNC_AP_CHANNEL, false, SYNC_AP_MAX_CONN);
  if (!started) {
    LOG_ERR("DSA", "Failed to start sync AP");
    {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_SYNC_ERROR);
    }
    requestUpdate();
    return;
  }
  delay(100);

  const IPAddress apIP = WiFi.softAPIP();
  char ipBuf[16];
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  sourceIP = ipBuf;

  // Captive portal DNS so any domain resolves to us
  syncDns.reset(new DNSServer());
  syncDns->setErrorReplyCode(DNSReplyCode::NoError);
  syncDns->start(DNS_PORT, "*", apIP);

  syncServer.reset(new DeviceSyncServer());
  syncServer->begin();

  if (!syncServer->isRunning()) {
    LOG_ERR("DSA", "Failed to start sync server");
    {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_SYNC_ERROR);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::SERVER_RUNNING;
  }
  requestUpdate();
  LOG_DBG("DSA", "Source mode ready, IP: %s", sourceIP.c_str());
}

// ---------------------------------------------------------------------------
// Target mode
// ---------------------------------------------------------------------------

void DeviceSyncActivity::startTargetMode() {
  WiFi.mode(WIFI_STA);

  {
    RenderLock lock(*this);
    state = State::WIFI_SELECTION;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           onWifiSelectionComplete(!result.isCancelled);
                         });
}

void DeviceSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    LOG_DBG("DSA", "WiFi selection cancelled");
    finish();
    return;
  }

  // The source device is always at the AP gateway address
  const IPAddress gateway = WiFi.gatewayIP();
  char ipBuf[16];
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);

  std::string baseUrl = std::string("http://") + ipBuf + ":" +
                        std::to_string(static_cast<int>(DeviceSyncServer::PORT));

  {
    RenderLock lock(*this);
    state = State::FETCHING_MANIFEST;
    statusMessage = tr(STR_SYNC_FETCHING);
  }
  requestUpdate(true);

  performDownload(baseUrl);
}

void DeviceSyncActivity::performDownload(const std::string& sourceBaseUrl) {
  // 1. Fetch manifest
  const std::string manifestUrl = sourceBaseUrl + "/sync/manifest";
  std::string manifestJson;

  if (!HttpDownloader::fetchUrl(manifestUrl, manifestJson)) {
    LOG_ERR("DSA", "Failed to fetch manifest from %s", manifestUrl.c_str());
    {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_SYNC_ERROR);
    }
    requestUpdate();
    return;
  }

  // 2. Parse manifest
  // Use a JsonDocument large enough for a typical manifest; document is
  // short-lived so heap use is temporary.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, manifestJson);
  if (err || !doc.is<JsonArray>()) {
    LOG_ERR("DSA", "Manifest parse error: %s", err ? err.c_str() : "not array");
    {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_SYNC_ERROR);
    }
    requestUpdate();
    return;
  }

  const JsonArray arr = doc.as<JsonArray>();
  filesTotal = static_cast<int>(arr.size());

  if (filesTotal == 0) {
    LOG_DBG("DSA", "Source has no files to sync");
    {
      RenderLock lock(*this);
      state = State::DONE;
      statusMessage = tr(STR_SYNC_NO_FILES);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::DOWNLOADING;
    filesDownloaded = 0;
  }
  requestUpdate(true);

  // 3. Download each file
  for (const JsonVariant& item : arr) {
    const char* path = item["path"] | "";
    if (!path || path[0] == '\0') {
      continue;
    }

    // Ensure the parent directory exists
    std::string pathStr(path);
    const size_t lastSlash = pathStr.rfind('/');
    if (lastSlash != std::string::npos) {
      const std::string dir = pathStr.substr(0, lastSlash);
      Storage.mkdir(dir.c_str());
    }

    // Build the URL. The path (e.g. /.crosspoint/epub_xxx/progress.bin) is safe
    // to pass as a query-string value without encoding: '/' and '.' are allowed
    // in query strings; only '&', '=', '#' would need escaping, and none appear
    // in .crosspoint paths.
    const std::string fileUrl = sourceBaseUrl + "/sync/file?path=" + pathStr;

    LOG_DBG("DSA", "Downloading: %s", path);
    const auto result = HttpDownloader::downloadToFile(fileUrl, pathStr);
    if (result != HttpDownloader::OK) {
      LOG_ERR("DSA", "Failed to download: %s (err=%d)", path, static_cast<int>(result));
      // Continue with other files rather than aborting
    }

    filesDownloaded++;
    requestUpdate(true);
    esp_task_wdt_reset();
  }

  {
    RenderLock lock(*this);
    state = State::DONE;
    statusMessage = tr(STR_SYNC_COMPLETE);
  }
  requestUpdate();
  LOG_INF("DSA", "Device sync complete: %d/%d files", filesDownloaded, filesTotal);
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void DeviceSyncActivity::loop() {
  // Source mode: pump the sync server
  if (state == State::SERVER_RUNNING) {
    if (syncDns) {
      syncDns->processNextRequest();
    }
    if (syncServer && syncServer->isRunning()) {
      for (int i = 0; i < SERVER_REQUESTS_PER_LOOP; i++) {
        syncServer->handleClient();
        esp_task_wdt_reset();
      }
    }
  }

  // Role selection
  if (state == State::ROLE_SELECTION) {
    buttonNavigator.onNext([this] {
      selectedRole = (selectedRole + 1) % ROLE_COUNT;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      selectedRole = (selectedRole + ROLE_COUNT - 1) % ROLE_COUNT;
      requestUpdate();
    });

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (selectedRole == 0) {
        startSourceMode();
      } else {
        startTargetMode();
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    return;
  }

  // Back button exits in terminal states
  if (state == State::SERVER_RUNNING || state == State::DONE || state == State::FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void DeviceSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DEVICE_SYNC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  if (state == State::ROLE_SELECTION) {
    static constexpr StrId roleNames[ROLE_COUNT] = {StrId::STR_SYNC_SOURCE, StrId::STR_SYNC_TARGET};
    static constexpr StrId roleDescs[ROLE_COUNT] = {StrId::STR_SYNC_SOURCE_DESC,
                                                     StrId::STR_SYNC_TARGET_DESC};

    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, ROLE_COUNT, selectedRole,
                 [](int i) { return std::string(I18N.get(roleNames[i])); },
                 [](int i) { return std::string(I18N.get(roleDescs[i])); }, nullptr);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == State::AP_STARTING) {
    const int top = (pageHeight - lineH) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_SYNC_STARTING_AP));

  } else if (state == State::SERVER_RUNNING) {
    int y = contentTop;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_SYNC_WAITING_TARGET), true,
                      EpdFontFamily::BOLD);
    y += lineH + metrics.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_SYNC_CONNECT_HINT));
    y += lineH + metrics.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, SYNC_AP_SSID, true, EpdFontFamily::BOLD);
    y += lineH + metrics.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y,
                      (std::string("IP: ") + sourceIP).c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == State::WIFI_SELECTION || state == State::FETCHING_MANIFEST) {
    const int top = (pageHeight - lineH) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, top, statusMessage.empty() ? tr(STR_CONNECTING) : statusMessage.c_str());

  } else if (state == State::DOWNLOADING) {
    int y = contentTop;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SYNC_DOWNLOADING_FILES), true, EpdFontFamily::BOLD);
    y += lineH + metrics.verticalSpacing * 2;

    // Progress bar
    if (filesTotal > 0) {
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2,
               metrics.progressBarHeight},
          filesDownloaded, filesTotal);
      y += metrics.progressBarHeight + metrics.verticalSpacing;

      char buf[32];
      snprintf(buf, sizeof(buf), tr(STR_SYNC_FILES_PROGRESS), filesDownloaded, filesTotal);
      renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    }

  } else if (state == State::DONE) {
    const int top = (pageHeight - lineH * 2) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_SYNC_COMPLETE), true, EpdFontFamily::BOLD);
    if (!statusMessage.empty() && statusMessage != std::string(tr(STR_SYNC_COMPLETE))) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + lineH + metrics.verticalSpacing,
                                statusMessage.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else if (state == State::FAILED) {
    const int top = (pageHeight - lineH * 2) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_SYNC_ERROR), true, EpdFontFamily::BOLD);
    if (!statusMessage.empty() && statusMessage != std::string(tr(STR_SYNC_ERROR))) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + lineH + metrics.verticalSpacing,
                                statusMessage.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
