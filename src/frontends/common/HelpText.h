// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>

enum class HelpFeature {
  separator,
  help_screen,
  cold_reboot,
  reload_config,
  hot_reset,
  open_apple,
  solid_apple,
  quit,
  disk_slot6,
  swap_disks,
  hard_drive_slot7,
  fullscreen,
  keyboard_rocker,
  debugger,
  screenshot,
  save_config,
  cycle_video,
  render_mode,
  snapshot,
  pause,
  scroll_lock,
  numpad_speed,
  mouse_capture,
};

struct HelpLine {
  HelpFeature feature;
  const char* text;
};

constexpr size_t help_header_line_count = 3;
constexpr size_t help_body_line_count = 25;
constexpr size_t help_total_line_count =
    help_header_line_count + help_body_line_count;

constexpr std::array<const char*, help_header_line_count> help_header_strings =
    {
        {
            "Welcome to LinApple - Apple][ emulator for Linux!",
            "Conf file is linapple.conf in current directory by default",
            "Archive of Apple ][ software: ftp.apple.asimov.net",
        },
};

constexpr std::array<HelpLine, help_body_line_count> help_body_lines = {
    {
        {HelpFeature::help_screen, "          F1 - Show this help screen"},
        {HelpFeature::cold_reboot, "     Ctrl+F2 - Cold reboot (Power cycle)"},
        {
            HelpFeature::reload_config,
            "    Shift+F2 - Reload configuration file and cold reboot",
        },
        {HelpFeature::hot_reset, "    Ctrl+F10 - Hot Reset (Control+Reset)"},
        {
            HelpFeature::open_apple,
            "    Left Alt - Open Apple (terminal: Alt+key)",
        },
        {
            HelpFeature::solid_apple,
            "   Right Alt - Solid Apple (not in the terminal)",
        },
        {HelpFeature::quit, "         F12 - Quit LinApple"},
        {HelpFeature::separator, ""},
        {
            HelpFeature::disk_slot6,
            "       F3/F4 - Load floppy disk 1/2 (Slot 6, Drive 1/2)",
        },
        {HelpFeature::swap_disks, "          F5 - Swap floppy disks"},
        {
            HelpFeature::hard_drive_slot7,
            " Shift+F3/F4 - Attach hard drive 1/2 (Slot 7, Drive 1/2)",
        },
        {HelpFeature::separator, ""},
        {HelpFeature::fullscreen, "          F6 - Toggle fullscreen mode"},
        {
            HelpFeature::keyboard_rocker,
            "    Shift+F6 - Toggle character set (keyboard rocker switch)",
        },
        {HelpFeature::debugger, "          F7 - Toggle debugging view"},
        {HelpFeature::screenshot, "          F8 - Take screenshot"},
        {
            HelpFeature::save_config,
            "    Shift+F8 - Save runtime changes to configuration file",
        },
        {
            HelpFeature::cycle_video,
            "          F9 - Cycle through various video modes",
        },
        {
            HelpFeature::render_mode,
            "    Shift+F9 - Budget video, for smoother music/audio",
        },
        {HelpFeature::snapshot, "     F10/F11 - Load/save snapshot file"},
        {HelpFeature::separator, ""},
        {HelpFeature::pause, "       Pause - Pause/resume emulator"},
        {
            HelpFeature::scroll_lock,
            "  ScrollLock - Toggle full speed (warp mode)",
        },
        {
            HelpFeature::numpad_speed,
            "Numpad +/-/* - Increase/Decrease/Normal speed",
        },
        {
            HelpFeature::mouse_capture,
            "Middle, Shift- or Ctrl-click - Capture or release the mouse",
        },
    },
};
