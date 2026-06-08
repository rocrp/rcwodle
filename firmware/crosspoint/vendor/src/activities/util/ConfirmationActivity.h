#pragma once
#include <functional>
#include <string>

#include "activities/Activity.h"
#include "fontIds.h"

class ConfirmationActivity : public Activity {
 private:
  // Input data
  std::string heading;
  std::string body;

  const int margin = 20;
  const int spacing = 30;
  const int fontId = UI_10_FONT_ID;

  std::string safeHeading;
  std::string safeBody;
  // WODLE-PORT: body is user/book/file text (filename, book title) — may be CJK
  // the builtin UI font lacks. Resolved once in onEnter via uiFontFor.
  int bodyFontId = UI_10_FONT_ID;
  int startY = 0;
  int lineHeight = 0;

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};