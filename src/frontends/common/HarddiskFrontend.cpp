// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/HarddiskFrontend.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "frontends/common/AppController.h"

namespace {

constexpr const char* k_harddisk_id = "linapple.harddisk";

int g_card_slot = harddisk_frontend_no_card;

auto log_reporter(int drive, int error, const char* message) -> void {
  Logger::error("hard disk drive %d: %s (%d)\n", drive + 1, message, error);
}

HarddiskErrorReporter_t g_reporter = log_reporter;

auto is_drive_valid(int drive) -> bool {
  return drive >= 0 && drive < harddisk_drive_count;
}

auto drive_error(int drive) -> int {
  HarddiskStatus_t status{};
  size_t size = sizeof(status);
  if (peripheral_query(g_card_slot, harddisk_query_status, &status, &size) !=
      peripheral_ok) {
    return harddisk_err_io;
  }
  return (drive == harddisk_drive_0) ? status.drive0_last_error
                                     : status.drive1_last_error;
}

// What the drive holds is known only once the queue has been drained, so the
// configuration is written after that and the error read from the same
// status.
auto settle_and_report(int drive) -> int {
  app_controller_save_harddisk_config(drive);
  const int error = drive_error(drive);
  if (error != harddisk_err_none && g_reporter != nullptr) {
    g_reporter(drive, error, harddisk_frontend_error_message(error));
  }
  return error;
}

}  // namespace

auto harddisk_frontend_initialize() -> void {
  g_card_slot = peripheral_slot_of(k_harddisk_id);
}

auto harddisk_frontend_slot() -> int { return g_card_slot; }

auto harddisk_frontend_insert(int drive, const char* path, bool write_protected)
    -> int {
  if (!is_drive_valid(drive) || path == nullptr) {
    return harddisk_err_io;
  }
  if (g_card_slot == harddisk_frontend_no_card) {
    Logger::error("hard disk drive %d: no hard disk is installed\n", drive + 1);
    return harddisk_frontend_no_card;
  }
  HarddiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  cmd.write_protected = write_protected ? 1 : 0;
  if (std::strlen(path) >= sizeof(cmd.path)) {
    return harddisk_err_not_found;
  }
  std::strncpy(cmd.path, path, sizeof(cmd.path) - 1);
  if (peripheral_command(g_card_slot, harddisk_cmd_insert, &cmd, sizeof(cmd)) !=
      peripheral_ok) {
    return harddisk_err_io;
  }
  return settle_and_report(drive);
}

auto harddisk_frontend_eject(int drive) -> int {
  if (!is_drive_valid(drive)) {
    return harddisk_err_io;
  }
  if (g_card_slot == harddisk_frontend_no_card) {
    Logger::error("hard disk drive %d: no hard disk is installed\n", drive + 1);
    return harddisk_frontend_no_card;
  }
  HarddiskEjectCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  if (peripheral_command(g_card_slot, harddisk_cmd_eject, &cmd, sizeof(cmd)) !=
      peripheral_ok) {
    return harddisk_err_io;
  }
  return settle_and_report(drive);
}

auto harddisk_frontend_error_message(int error) -> const char* {
  switch (error) {
    case harddisk_err_none:
      return "no error";
    case harddisk_err_not_found:
      return "file not found or unreadable";
    case harddisk_err_io:
      return "could not open or read the image";
    case harddisk_err_read_only:
      return "the image is read-only";
    case harddisk_err_invalid_format:
      return "not a hard disk image: the size is not a multiple of 512 bytes, "
             "or the 2MG header is malformed";
    case harddisk_frontend_no_card:
      return "no hard disk is installed";
    default:
      return "unknown error";
  }
}

auto harddisk_frontend_set_error_reporter(HarddiskErrorReporter_t reporter)
    -> void {
  g_reporter = (reporter != nullptr) ? reporter : log_reporter;
}
