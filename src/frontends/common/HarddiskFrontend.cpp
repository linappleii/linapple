// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/HarddiskFrontend.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

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

HarddiskErrorReporter_t g_reporter = nullptr;

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
// status. The log line is the one report every frontend gives, the terminal
// included; a reporter only adds a place to show it.
auto settle_and_report(int drive, const char* inserted_path, bool record)
    -> int {
  if (record) {
    app_controller_save_harddisk_config(drive);
  } else {
    peripheral_manager_think(0);
  }
  const int error = drive_error(drive);
  if (error == harddisk_err_none) {
    return error;
  }
  const char* message = harddisk_frontend_error_message(error);
  if (inserted_path != nullptr) {
    Logger::error("could not insert hard disk image '%s': %s\n", inserted_path,
                  message);
  } else {
    Logger::error("hard disk drive %d: %s\n", drive + 1, message);
  }
  if (g_reporter != nullptr) {
    g_reporter(drive, error, message);
  }
  return error;
}

auto insert(int drive, const char* path, bool write_protected, bool record)
    -> int {
  if (!is_drive_valid(drive) || path == nullptr) {
    return harddisk_err_io;
  }
  if (g_card_slot == harddisk_frontend_no_card) {
    Logger::error("hard disk drive %d: no hard disk is installed\n", drive + 1);
    return harddisk_frontend_no_card;
  }
  // The caller may hand over the configuration's own path field, which
  // recording the insert rewrites, so the path is copied before anything
  // else happens.
  const std::string inserted(path);
  HarddiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  cmd.write_protected = write_protected ? 1 : 0;
  if (inserted.size() >= sizeof(cmd.path)) {
    return harddisk_err_not_found;
  }
  std::strncpy(cmd.path, inserted.c_str(), sizeof(cmd.path) - 1);
  if (peripheral_command(g_card_slot, harddisk_cmd_insert, &cmd, sizeof(cmd)) !=
      peripheral_ok) {
    return harddisk_err_io;
  }
  return settle_and_report(drive, inserted.c_str(), record);
}

}  // namespace

auto harddisk_frontend_initialize() -> void {
  g_card_slot = peripheral_slot_of(k_harddisk_id);
}

auto harddisk_frontend_slot() -> int { return g_card_slot; }

auto harddisk_frontend_insert(int drive, const char* path, bool write_protected)
    -> int {
  return insert(drive, path, write_protected, true);
}

auto harddisk_frontend_insert_for_run(int drive, const char* path) -> int {
  return insert(drive, path, false, false);
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
  return settle_and_report(drive, nullptr, true);
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
    case harddisk_err_not_block_image:
      return "a nibble or flux image holds no blocks; use the Disk II";
    case harddisk_frontend_no_card:
      return "no hard disk is installed";
    default:
      return "unknown error";
  }
}

auto harddisk_frontend_set_error_reporter(HarddiskErrorReporter_t reporter)
    -> void {
  g_reporter = reporter;
}
