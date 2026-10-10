// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <string>
#include <utility>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr int slot_6 = 6;

// Declared rather than inherited: with no configuration the slot fallbacks in
// peripheral_register_internal supply a printer, a Super Serial Card and a
// Mockingboard beside the Disk II, none of which these cases touch.
using TestConfig = TestFixtures::ScopedTestConfig;

class DiskProtHarness {
 public:
  DiskProtHarness() {
    machine_.load();
    linapple_init();
    peripheral_manager_init();
    linapple_register_peripherals();
  }

  ~DiskProtHarness() { linapple_shutdown(); }

  DiskProtHarness(const DiskProtHarness&) = delete;
  auto operator=(const DiskProtHarness&) -> DiskProtHarness& = delete;
  DiskProtHarness(DiskProtHarness&&) = delete;
  auto operator=(DiskProtHarness&&) -> DiskProtHarness& = delete;

  static auto insert_disk(const std::string& path, bool write_protected = false)
      -> void {
    DiskInsertCmd cmd{};
    cmd.drive = disk_drive_0;
    util_safe_strcpy(cmd.path, path.c_str(), disk_insert_path_max);
    cmd.write_protected = write_protected ? 1 : 0;
    peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
    peripheral_manager_think(0);
  }

  static auto get_status() -> DiskStatus {
    DiskStatus status{};
    size_t size = sizeof(status);
    peripheral_query(slot_6, disk_query_status, &status, &size);
    return status;
  }

 private:
  TestConfig machine_{TestConfig::disk_ii_only()};
};

class ScopedFileMode {
 public:
  ScopedFileMode(std::string path, mode_t new_mode,
                   mode_t restore_mode = 0644)
      : path_(std::move(path)), restore_mode_(restore_mode) {
    chmod(path_.c_str(), new_mode);
  }

  ~ScopedFileMode() {
    if (!path_.empty()) {
      chmod(path_.c_str(), restore_mode_);
    }
  }

  ScopedFileMode(const ScopedFileMode&) = delete;
  auto operator=(const ScopedFileMode&) -> ScopedFileMode& = delete;
  ScopedFileMode(ScopedFileMode&&) = delete;
  auto operator=(ScopedFileMode&&) -> ScopedFileMode& = delete;

 private:
  std::string path_;
  mode_t restore_mode_;
};

}  // namespace

TEST_CASE("DiskIntegration: [PROT-01] Three-Layer Write Protection") {
  DiskProtHarness harness;

  auto user_disk = TestFixtures::create_ephemeral("minimal.dsk");
  auto os_disk = TestFixtures::create_ephemeral("minimal.dsk");
  auto format_disk = TestFixtures::create_ephemeral("minimal.woz");
  auto rw_disk = TestFixtures::create_ephemeral("minimal.dsk");

  // Layer 3: User runtime toggle (cmd.write_protected = true)
  {
    harness.insert_disk(user_disk.path(), true);
    const DiskStatus status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 1);
    CHECK(status.drive0_last_error == disk_err_none);
  }

  // Layer 2: OS file permissions (read-only file on filesystem)
  // Superuser (root / container environments) bypasses DAC read-only
  // permissions.
  if (getuid() != 0) {
    ScopedFileMode readonly_guard(os_disk.path(), 0444, 0644);
    harness.insert_disk(os_disk.path(), false);
    const DiskStatus status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 1);
    CHECK(status.drive0_last_error == disk_err_none);
  }

  // Layer 1: Format/Driver Capability (WOZ2 is read-only in LinApple)
  {
    harness.insert_disk(format_disk.path(), false);
    const DiskStatus status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 1);
    CHECK(status.drive0_last_error == disk_err_none);
  }

  // Baseline: Writable disk image is not write-protected
  {
    harness.insert_disk(rw_disk.path(), false);
    const DiskStatus status = harness.get_status();
    CHECK(status.drive0_loaded == 1);
    CHECK(status.drive0_write_protected == 0);
    CHECK(status.drive0_last_error == disk_err_none);
  }
}
