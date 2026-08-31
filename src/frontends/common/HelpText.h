#pragma once

#include <array>
#include <cstddef>

constexpr size_t HELP_HEADER_LINE_COUNT = 3;
constexpr size_t HELP_BODY_LINE_COUNT = 22;
constexpr size_t HELP_TOTAL_LINE_COUNT =
    HELP_HEADER_LINE_COUNT + HELP_BODY_LINE_COUNT;

constexpr std::array<const char*, HELP_HEADER_LINE_COUNT> HELP_HEADER_STRINGS =
    {{"Welcome to LinApple - Apple][ emulator for Linux!",
      "Conf file is linapple.conf in current directory by default",
      "Archive of Apple ][ software: ftp.apple.asimov.net"}};

constexpr std::array<const char*, HELP_BODY_LINE_COUNT> HELP_BODY_STRINGS = {{
    "          F1 - Show this help screen",
    "     Ctrl+F2 - Cold reboot (Power cycle)",
    "    Shift+F2 - Reload configuration file and cold reboot",
    "    Ctrl+F10 - Hot Reset (Control+Reset)",
    "         F12 - Quit LinApple",
    "",
    "       F3/F4 - Load floppy disk 1/2 (Slot 6, Drive 1/2)",
    "          F5 - Swap floppy disks",
    " Shift+F3/F4 - Attach hard drive 1/2 (Slot 7, Drive 1/2)",
    "",
    "          F6 - Toggle fullscreen mode",
    "    Shift+F6 - Toggle character set (keyboard rocker switch)",
    "          F7 - Toggle debugging view",
    "          F8 - Take screenshot",
    "    Shift+F8 - Save runtime changes to configuration file",
    "          F9 - Cycle through various video modes",
    "    Shift+F9 - Budget video, for smoother music/audio",
    "     F10/F11 - Load/save snapshot file",
    "",
    "       Pause - Pause/resume emulator",
    "  ScrollLock - Toggle full speed (warp mode)",
    "Numpad +/-/* - Increase/Decrease/Normal speed",
}};
