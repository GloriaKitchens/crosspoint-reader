#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "BookmarkStore.h"
#include "util/ButtonNavigator.h"

class EpubReaderBookmarkActivity final : public Activity {
 public:
  explicit EpubReaderBookmarkActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                      const std::shared_ptr<Epub>& epub, const BookmarkStore& store)
      : Activity("EpubReaderBookmark", renderer, mappedInput), epub(epub), store(store) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::shared_ptr<Epub> epub;
  const BookmarkStore& store;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int getPageItems() const;
};
