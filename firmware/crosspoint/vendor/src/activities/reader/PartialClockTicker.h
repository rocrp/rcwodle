/* WODLE-PORT: minute-fresh status-bar clock via DU partial-window refresh
 * (spi_epd_demo recipe — see HalDisplay::refreshWindow). EXPERIMENTAL:
 * Settings → Status Bar → Live Clock, default OFF until a HIL soak proves
 * the partial waveform on this panel.
 *
 * Call tick() from a reader activity's loop(). On a minute boundary it
 * white-fills the status-bar strip, re-runs the activity's renderStatusBar()
 * and partial-refreshes just that strip — no page-turn DU, no full flash.
 * The backend refuses the window while the controller RAMs hold 4-gray AA
 * planes (returns false); the next full refresh re-arms it.
 *
 * Wired into the EPUB and TXT readers. The XTC reader is INTENTIONALLY
 * skipped: its status bar is an overlay composited onto pre-rendered page
 * images (renderStatusBarOverlay, top OR bottom), so a white-fill+redraw
 * would erase page pixels under the strip — a correct partial clock there
 * needs the page image re-blitted first. Revisit post-HIL if XTC matters. */
#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalClock.h>

#include "activities/RenderLock.h"
#include "components/UITheme.h"

class PartialClockTicker {
  uint8_t lastMinute_ = 0xFF;

 public:
  template <typename DrawStatusBarFn>
  void tick(GfxRenderer& renderer, DrawStatusBarFn drawStatusBar) {
    if (!SETTINGS.clockPartialRefresh || !SETTINGS.statusBarClock || !halClock.isAvailable()) return;
    uint8_t h = 0, m = 0;
    if (!halClock.getTime(h, m)) return;  // clock not set yet
    if (lastMinute_ == m) return;
    const bool firstObservation = lastMinute_ == 0xFF;
    lastMinute_ = m;
    if (firstObservation) return;  // page was just rendered with the current time

    RenderLock lock;
    int top = 0, right = 0, bottom = 0, left = 0;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    // Strip = everything drawStatusBar paints: text row (statusBarHeight above
    // the bottom margin) down to the screen edge, plus a small safety margin.
    const int stripTop =
        renderer.getScreenHeight() - UITheme::getInstance().getStatusBarHeight() - bottom - 28;
    if (stripTop < 0) return;
    const int stripH = renderer.getScreenHeight() - stripTop;
    renderer.fillRect(0, stripTop, renderer.getScreenWidth(), stripH, false);
    drawStatusBar();
    renderer.displayWindow(0, stripTop, renderer.getScreenWidth(), stripH);
  }
};
