#pragma once

#include <array>
#include <cstddef>

enum class HelpFeature_t {
  separator,
  help_screen,
  cold_reboot,
  reload_config,
  hot_reset,
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
};

struct HelpLine_t {
  HelpFeature_t feature;
  const char* text;
};

constexpr size_t HELP_HEADER_LINE_COUNT = 3;
constexpr size_t HELP_BODY_LINE_COUNT = 22;
constexpr size_t HELP_TOTAL_LINE_COUNT =
    HELP_HEADER_LINE_COUNT + HELP_BODY_LINE_COUNT;

constexpr std::array<const char*, HELP_HEADER_LINE_COUNT> HELP_HEADER_STRINGS =
    {{"Welcome to LinApple - Apple][ emulator for Linux!",
      "Conf file is linapple.conf in current directory by default",
      "Archive of Apple ][ software: ftp.apple.asimov.net"}};

constexpr std::array<HelpLine_t, HELP_BODY_LINE_COUNT> HELP_BODY_LINES = {{
    {HelpFeature_t::help_screen, "          F1 - Show this help screen"},
    {HelpFeature_t::cold_reboot, "     Ctrl+F2 - Cold reboot (Power cycle)"},
    {HelpFeature_t::reload_config,
     "    Shift+F2 - Reload configuration file and cold reboot"},
    {HelpFeature_t::hot_reset, "    Ctrl+F10 - Hot Reset (Control+Reset)"},
    {HelpFeature_t::quit, "         F12 - Quit LinApple"},
    {HelpFeature_t::separator, ""},
    {HelpFeature_t::disk_slot6,
     "       F3/F4 - Load floppy disk 1/2 (Slot 6, Drive 1/2)"},
    {HelpFeature_t::swap_disks, "          F5 - Swap floppy disks"},
    {HelpFeature_t::hard_drive_slot7,
     " Shift+F3/F4 - Attach hard drive 1/2 (Slot 7, Drive 1/2)"},
    {HelpFeature_t::separator, ""},
    {HelpFeature_t::fullscreen, "          F6 - Toggle fullscreen mode"},
    {HelpFeature_t::keyboard_rocker,
     "    Shift+F6 - Toggle character set (keyboard rocker switch)"},
    {HelpFeature_t::debugger, "          F7 - Toggle debugging view"},
    {HelpFeature_t::screenshot, "          F8 - Take screenshot"},
    {HelpFeature_t::save_config,
     "    Shift+F8 - Save runtime changes to configuration file"},
    {HelpFeature_t::cycle_video,
     "          F9 - Cycle through various video modes"},
    {HelpFeature_t::render_mode,
     "    Shift+F9 - Budget video, for smoother music/audio"},
    {HelpFeature_t::snapshot, "     F10/F11 - Load/save snapshot file"},
    {HelpFeature_t::separator, ""},
    {HelpFeature_t::pause, "       Pause - Pause/resume emulator"},
    {HelpFeature_t::scroll_lock,
     "  ScrollLock - Toggle full speed (warp mode)"},
    {HelpFeature_t::numpad_speed,
     "Numpad +/-/* - Increase/Decrease/Normal speed"},
}};
