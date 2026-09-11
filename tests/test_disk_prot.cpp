// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <string>
#include <utility>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr int slot_6 = 6;

class DiskProtHarness_t {
 public:
  DiskProtHarness_t() {
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();
  }

  ~DiskProtHarness_t() { linapple_shutdown(); }

  DiskProtHarness_t(const DiskProtHarness_t&) = delete;
  auto operator=(const DiskProtHarness_t&) -> DiskProtHarness_t& = delete;
  DiskProtHarness_t(DiskProtHarness_t&&) = delete;
  auto operator=(DiskProtHarness_t&&) -> DiskProtHarness_t& = delete;

  auto insert_disk(const std::string& path, bool write_protected = false)
      -> void {
    DiskInsertCmd_t cmd{};
    cmd.drive = disk_drive_0;
    util_safe_strcpy(cmd.path, path.c_str(), disk_insert_path_max);
    cmd.write_protected = write_protected ? 1 : 0;
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);
  }

  auto get_status() const -> DiskStatus_t {
    DiskStatus_t status{};
    size_t size = sizeof(status);
    peripheral_query(slot_6, disk_cmd_get_status, &status, &size);
    return status;
  }
};

class ScopedFileMode_t {
 public:
  ScopedFileMode_t(std::string path, mode_t new_mode,
                   mode_t restore_mode = 0644)
      : path_(std::move(path)), restore_mode_(restore_mode) {
    chmod(path_.c_str(), new_mode);
  }

  ~ScopedFileMode_t() {
    if (!path_.empty()) {
      chmod(path_.c_str(), restore_mode_);
    }
  }

  ScopedFileMode_t(const ScopedFileMode_t&) = delete;
  auto operator=(const ScopedFileMode_t&) -> ScopedFileMode_t& = delete;
  ScopedFileMode_t(ScopedFileMode_t&&) = delete;
  auto operator=(ScopedFileMode_t&&) -> ScopedFileMode_t& = delete;

 private:
  std::string path_;
  mode_t restore_mode_;
};

}  // namespace

TEST_CASE("DiskIntegration: [PROT-01] Three-Layer Write Protection") {
  DiskProtHarness_t harness;

  auto user_disk = TestFixtures::create_ephemeral("minimal.dsk");
  auto os_disk = TestFixtures::create_ephemeral("minimal.dsk");
  auto format_disk = TestFixtures::create_ephemeral("minimal.woz");
  auto rw_disk = TestFixtures::create_ephemeral("minimal.dsk");

  // Layer 3: User runtime toggle (cmd.write_protected = true)
  {
    harness.insert_disk(user_disk.path(), true);
    const DiskStatus_t status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 1);
    CHECK(status.drive0_last_error == disk_err_none);
  }

  // Layer 2: OS file permissions (read-only file on filesystem)
  // Superuser (root / container environments) bypasses DAC read-only
  // permissions.
  if (getuid() != 0) {
    ScopedFileMode_t readonly_guard(os_disk.path(), 0444, 0644);
    harness.insert_disk(os_disk.path(), false);
    const DiskStatus_t status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 1);
    CHECK(status.drive0_last_error == disk_err_none);
  }

  // Layer 1: Format/Driver Capability (WOZ2 is read-only in LinApple)
  {
    harness.insert_disk(format_disk.path(), false);
    const DiskStatus_t status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 1);
    CHECK(status.drive0_last_error == disk_err_none);
  }

  // Baseline: Writable disk image is not write-protected
  {
    harness.insert_disk(rw_disk.path(), false);
    const DiskStatus_t status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 0);
    CHECK(status.drive0_last_error == disk_err_none);
  }
}
