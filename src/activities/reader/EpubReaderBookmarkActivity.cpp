#include "EpubReaderBookmarkActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int EpubReaderBookmarkActivity::getPageItems() const {
  constexpr int startY = 60;
  constexpr int lineHeight = 30;
  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int availableHeight = screenHeight - (startY + hintGutterHeight) - lineHeight;
  return std::max(1, availableHeight / lineHeight);
}

void EpubReaderBookmarkActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void EpubReaderBookmarkActivity::onExit() { Activity::onExit(); }

void EpubReaderBookmarkActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (store.count() > 0 && selectedIndex >= 0 && selectedIndex < store.count()) {
      const Bookmark& bm = store.get(selectedIndex);
      setResult(BookmarkResult{bm.spineIndex, bm.page});
      finish();
    }
    return;
  }

  const int total = store.count();
  const int pageItems = getPageItems();

  buttonNavigator.onNextRelease([this, total] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, total] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, total, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, total, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, total, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, total, pageItems);
    requestUpdate();
  });
}

void EpubReaderBookmarkActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  // Title
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_BOOKMARKS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_BOOKMARKS), true, EpdFontFamily::BOLD);

  if (store.count() == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, 90 + contentY, tr(STR_NO_BOOKMARKS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  constexpr int startY = 60;
  constexpr int lineHeight = 30;
  const int pageItems = getPageItems();
  const int total = store.count();

  const int pageStartIndex = (total > 0) ? (selectedIndex / pageItems * pageItems) : 0;

  for (int i = 0; i < pageItems; i++) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= total) break;

    const int displayY = startY + contentY + i * lineHeight;
    const bool isSelected = (itemIndex == selectedIndex);

    if (isSelected) {
      renderer.fillRect(contentX, displayY, contentWidth - 1, lineHeight, true);
    }

    const Bookmark& bm = store.get(itemIndex);

    // Build label: "Chapter Title  p.N" or fallback
    std::string chapterLabel;
    if (epub) {
      const int tocIndex = epub->getTocIndexForSpineIndex(static_cast<int>(bm.spineIndex));
      if (tocIndex >= 0) {
        const auto tocItem = epub->getTocItem(tocIndex);
        chapterLabel = tocItem.title;
      }
    }
    if (chapterLabel.empty()) {
      chapterLabel = std::string(tr(STR_SECTION_PREFIX)) + std::to_string(bm.spineIndex + 1);
    }

    // Page number suffix
    char pageSuffix[16];
    snprintf(pageSuffix, sizeof(pageSuffix), "  p.%u", static_cast<unsigned>(bm.page + 1));

    const int suffixWidth = renderer.getTextWidth(UI_10_FONT_ID, pageSuffix);
    const int maxLabelWidth = contentWidth - 40 - suffixWidth;
    const std::string truncated = renderer.truncatedText(UI_10_FONT_ID, chapterLabel.c_str(), maxLabelWidth);

    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, truncated.c_str(), !isSelected);
    renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - 20 - suffixWidth, displayY, pageSuffix, !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
