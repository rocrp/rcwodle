#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const { gpio.update(); }
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;
  // WODLE-PORT: raw tap-coordinate delivery, the foundation for direct
  // tap-to-select (additive — the zone->button synthesis still drives the
  // existing button navigation untouched). Edge/clear-on-read: returns true at
  // most once per physical tap, writing the tap's logical-portrait (x,y); false
  // (x/y untouched) when no tap is pending. Non-const because consuming the tap
  // clears the latch. Coords are logical portrait; orientation mapping (the
  // reader can rotate) is the caller's later concern.
  bool consumeTap(int& x, int& y) const { return gpio.consumeTap(x, y); }

 private:
  HalGPIO& gpio;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};
