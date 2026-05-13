#include "apple2/SaveState.h"

#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/SerialComms.h"
#include "apple2/peripherals/Speaker.h"
#include "apple2/Structs.h"
#include "apple2/Video.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Save-state (snapshot) module
 */

#define DEFAULT_SNAPSHOT_NAME "SaveState.aws"

bool g_bSaveStateOnExit = false;

static char g_szSaveStateFilename[PATH_MAX_LEN] = {0};

auto Snapshot_GetFilename() -> char* { return g_szSaveStateFilename; }

void Snapshot_SetFilename(const char* pszFilename) {
  if (pszFilename && *pszFilename) {
    strcpy(g_szSaveStateFilename, pszFilename);
  } else {
    g_szSaveStateFilename[0] = '\0';
  }
}

void Snapshot_LoadState() {
  char szMessage[32 + PATH_MAX_LEN];
  szMessage[0] = '\0';

  auto pSS = std::unique_ptr<APPLEWIN_SNAPSHOT>(new APPLEWIN_SNAPSHOT());

  try {
    FilePtr hFile(fopen(g_szSaveStateFilename, "rb"), fclose);

    if (!hFile) {
      strcpy(szMessage, "File not found: ");
      strcpy(szMessage + strlen(szMessage), g_szSaveStateFilename);
      throw(0);
    }

    uint32_t dwBytesRead = 0;
    dwBytesRead = static_cast<uint32_t>(
        fread(pSS.get(), 1, sizeof(APPLEWIN_SNAPSHOT), hFile.get()));
    bool bRes = (dwBytesRead == sizeof(APPLEWIN_SNAPSHOT));

    if (!bRes || (dwBytesRead != sizeof(APPLEWIN_SNAPSHOT))) {
      // File size wrong: probably because of version mismatch or corrupt file
      strcpy(szMessage, "File size mismatch");
      throw(0);
    }

    if (pSS->Hdr.dwTag != static_cast<uint32_t> AW_SS_TAG) {
      strcpy(szMessage, "File corrupt");
      throw(0);
    }

    /* Let it be any version, never mind it! ^_^ */
    if (pSS->Hdr.dwVersion != MAKE_VERSION(1, 0, 0, 1)) {
      strcpy(szMessage, "Version mismatch");
      throw(0);
    }

    // Verify peripheral manifest
    if (!Peripheral_VerifyManifest(&pSS->Manifest)) {
      strcpy(szMessage, "Hardware configuration mismatch - load aborted");
      throw(0);
    }

    // Reset all sub-systems
    MemReset();

    if (!IS_APPLE2()) {
      MemResetPaging();
    }

    Peripheral_Manager_Reset();
    VideoResetState();

    CpuSetSnapshot(&pSS->Apple2Unit.CPU6502);
    {
      size_t size = sizeof(pSS->Apple2Unit.Joystick);
      Peripheral_LoadState(0, &pSS->Apple2Unit.Joystick, size);
    }
    Peripheral_LoadStateByName(0, "Keyboard", &pSS->Apple2Unit.Keyboard,
                               sizeof(pSS->Apple2Unit.Keyboard));
    VideoSetSnapshot(&pSS->Apple2Unit.Video);
    MemSetSnapshot(&pSS->Apple2Unit.Memory);

    // Slots 0-7
    for (int i = 0; i < 8; ++i) {
      void* slot_state = nullptr;
      size_t slot_size = 0;
      const char* name = nullptr;
      switch (i) {
        case 0:
          name = "Speaker";
          slot_state = &pSS->Apple2Unit.Speaker;
          slot_size = sizeof(pSS->Apple2Unit.Speaker);
          break;
        case 1:
          slot_state = &pSS->Empty1;
          slot_size = sizeof(pSS->Empty1);
          break;
        case 2:
          slot_state = &pSS->Apple2Unit.Comms;
          slot_size = sizeof(pSS->Apple2Unit.Comms);
          break;
        case 3:
          slot_state = &pSS->Empty3;
          slot_size = sizeof(pSS->Empty3);
          break;
        case 4:
          slot_state = &pSS->Mockingboard1;
          slot_size = sizeof(pSS->Mockingboard1);
          break;
        case 5:
          slot_state = &pSS->Mockingboard2;
          slot_size = sizeof(pSS->Mockingboard2);
          break;
        case 6:
          break;  // Slot 6 handled via manifest/ABI
        case 7:
          slot_state = &pSS->Empty7;
          slot_size = sizeof(pSS->Empty7);
          break;
      }
      if (slot_state) {
        if (name)
          Peripheral_LoadStateByName(i, name, slot_state, slot_size);
        else
          Peripheral_LoadState(i, slot_state, slot_size);
      }
    }

    // Hmmm. And SLOT 7 (HDD1 and HDD2)? Where are they??? -- beom beotiger ^_^
  } catch (int) {
    fprintf(
        stderr, "ERROR: %s\n",
        szMessage);  // instead of wndzoooe messagebox let's use powerful stderr

    // Ensure system is in a clean state even if load fails
    if (mem) {
      MemReset();
      if (!IS_APPLE2()) {
        MemResetPaging();
      }
    }
    CpuReset();
    Peripheral_Manager_Reset();
    VideoResetState();
  }
}

void Snapshot_SaveState() {
  auto pSS = std::unique_ptr<APPLEWIN_SNAPSHOT>(new APPLEWIN_SNAPSHOT());

  pSS->Hdr.dwTag = AW_SS_TAG;
  pSS->Hdr.dwVersion = MAKE_VERSION(1, 0, 0, 1);
  pSS->Hdr.dwChecksum = 0;  // TO DO

  // Apple2 uint
  pSS->Apple2Unit.UnitHdr.dwLength = sizeof(SS_APPLE2_Unit);
  pSS->Apple2Unit.UnitHdr.dwVersion = MAKE_VERSION(1, 0, 0, 0);

  Peripheral_GetManifest(&pSS->Manifest);

  CpuGetSnapshot(&pSS->Apple2Unit.CPU6502);
  {
    size_t size = sizeof(pSS->Apple2Unit.Joystick);
    Peripheral_SaveState(0, &pSS->Apple2Unit.Joystick, &size);
  }
  VideoGetSnapshot(&pSS->Apple2Unit.Video);
  MemGetSnapshot(&pSS->Apple2Unit.Memory);

  // Use Peripheral_SaveStateByName for Keyboard (Slot 0)
  size_t kbd_size = sizeof(pSS->Apple2Unit.Keyboard);
  Peripheral_SaveStateByName(0, "Keyboard", &pSS->Apple2Unit.Keyboard,
                             &kbd_size);

  // Slots 0-7
  for (int i = 0; i < 8; ++i) {
    void* slot_state = nullptr;
    size_t slot_size = 0;
    const char* name = nullptr;
    switch (i) {
      case 0:
        name = "Speaker";
        slot_state = &pSS->Apple2Unit.Speaker;
        slot_size = sizeof(pSS->Apple2Unit.Speaker);
        break;
      case 1:
        slot_state = &pSS->Empty1;
        slot_size = sizeof(pSS->Empty1);
        break;
      case 2:
        slot_state = &pSS->Apple2Unit.Comms;
        slot_size = sizeof(pSS->Apple2Unit.Comms);
        break;
      case 3:
        slot_state = &pSS->Empty3;
        slot_size = sizeof(pSS->Empty3);
        break;
      case 4:
        slot_state = &pSS->Mockingboard1;
        slot_size = sizeof(pSS->Mockingboard1);
        break;
      case 5:
        slot_state = &pSS->Mockingboard2;
        slot_size = sizeof(pSS->Mockingboard2);
        break;
      case 6:
        break;  // Slot 6 handled via manifest/ABI
      case 7:
        slot_state = &pSS->Empty7;
        slot_size = sizeof(pSS->Empty7);
        break;
    }
    if (slot_state) {
      if (name)
        Peripheral_SaveStateByName(i, name, slot_state, &slot_size);
      else
        Peripheral_SaveState(i, slot_state, &slot_size);
    }
  }

  const char* pszFilename = g_szSaveStateFilename;
  if (pszFilename[0] == '\0') {
    pszFilename = DEFAULT_SNAPSHOT_NAME;
  }

  FilePtr hFile(fopen(pszFilename, "wb"), fclose);

  if (hFile) {
    fwrite(pSS.get(), 1, sizeof(APPLEWIN_SNAPSHOT), hFile.get());
    Logger::Info("Saved state to: %s\n", pszFilename);
  } else {
    Logger::Error("Failed to open save state file for writing: %s\n",
                  pszFilename);
  }
}

void Snapshot_Startup() {
  static bool bDone = false;

  if (bDone) {
    return;
  }

  if (g_szSaveStateFilename[0] != '\0') {
    // Explicit filename provided (e.g. from CLI)
    Snapshot_LoadState();
  } else if (g_bSaveStateOnExit) {
    // Automatic load at startup, only if default file exists
    if (access(DEFAULT_SNAPSHOT_NAME, F_OK) == 0) {
      Snapshot_SetFilename(DEFAULT_SNAPSHOT_NAME);
      Snapshot_LoadState();
    }
  }

  bDone = true;
}

void Snapshot_Shutdown() {
  static bool bDone = false;
  if (!g_bSaveStateOnExit || bDone) {
    return;
  }

  Snapshot_SaveState();

  bDone = true;
}
