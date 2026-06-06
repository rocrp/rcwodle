#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)
// WODLE-PORT: restart into USB mass-storage mode (SD exposed to the host,
// never mounted locally). Triggered from the home menu's File Transfer item.
void wodleEnterUsbTransfer();
