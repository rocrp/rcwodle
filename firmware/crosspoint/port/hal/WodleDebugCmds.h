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
bool consumePendingDump();                          /* loop() */
/* Pending partial-window refresh experiment (`wodle partial x y w h`,
 * PANEL coords: x along the 792 source axis, y = gate row). */
struct PartialRect
{
    int x, y, w, h;
};
bool consumePendingPartial(PartialRect &out); /* loop() */
/* Stream the 1-bit framebuffer over the console as base64 between
 * WODLE_DUMP_BEGIN/END markers (tools/wodle_console.py decodes to PNG).
 * Main thread only — reads the live framebuffer. */
void emitFrameDump(const uint8_t *fb, uint32_t size, int width, int height);
/* True while auto-sleep should be held off (debug session or nosleep on). */
bool sleepInhibited();

/* WODLE-PORT USB-CDC console plumbing: every command reply (incl. the
 * `wodle dump` stream) goes to the uart console AND, when set, this sink.
 * runCommandLine tokenizes in place and accepts both "wodle stat" and bare
 * "stat" — the CDC rx thread feeds it whole lines. */
void setReplySink(void (*sink)(const char *text));
int runCommandLine(char *line);
} // namespace WodleDebugCmds
