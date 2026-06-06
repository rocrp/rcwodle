/* WODLE-PORT: `wodle` MSH console command — remote HIL driver. Lets a host
 * script (or a human on the WCH-Link UART) drive the device:
 *
 *   wodle key <up|down|left|right|confirm|back|power> [holdMs]
 *   wodle open <path>      # open a book (validated on the main thread)
 *   wodle shot             # screenshot BMP to SD (main thread, RenderLock)
 *   wodle stat             # one-line machine-parseable status
 *   wodle nosleep <on|off> # force auto-sleep inhibit (off also clears the
 *                          # implicit debug-session inhibit)
 *
 * Thread model: handlers run on finsh's tshell thread and only ever touch a
 * mailbox guarded by short critical sections. The main loop consumes:
 * key injections inside HalGPIO::update() (so they ride the real input
 * pipeline), open/shot in loop() (they need activityManager/renderer).
 * Any command except `stat` marks the boot as a debug session, which
 * inhibits the auto-sleep timeout (a sleeping device ends the session). */
#pragma once

#include "WodleDebugCmdCore.h"

#include <string>

namespace WodleDebugCmds
{
/* main-thread consumers */
bool dequeueKey(WodleDebugCmdCore::KeyInject &out); /* HalGPIO::update() */
bool consumePendingOpen(std::string &path);         /* loop() */
bool consumePendingShot();                          /* loop() */
/* True while auto-sleep should be held off (debug session or nosleep on). */
bool sleepInhibited();
} // namespace WodleDebugCmds
