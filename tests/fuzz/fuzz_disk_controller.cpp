// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
#include <unistd.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"

extern "C" const char* __asan_default_options() { return "detect_leaks=1"; }

namespace {

constexpr int card_slot = 6;
constexpr uint16_t card_page = 0xC0E0;
constexpr size_t record_size = 6;
constexpr uint32_t max_slice_cycles = 65536;
constexpr uint32_t medium_cell_count = 50464;
constexpr size_t sector_size = 256;
constexpr size_t track_data_size = sectors_per_track * sector_size;

// One synthesised DOS 3.3 track, laid out as cells once and handed to every
// input. The medium is fixed so a crash the fuzzer finds is a property of the
// access sequence rather than of a track it also had to invent.
std::vector<uint8_t> medium_cells;

bool host_saw_null = false;

auto build_medium() -> void {
  std::vector<uint8_t> sectors(track_data_size, 0);
  for (size_t index = 0; index < sectors.size(); ++index) {
    sectors[index] = static_cast<uint8_t>(index * 7);
  }
  std::vector<uint8_t> nibbles(nibbles_per_track, 0);
  std::vector<uint8_t> sync_mask(nibbles_per_track, 0);
  std::vector<uint8_t> scratch(disk_encoding_scratch_size, 0);
  uint32_t nibble_count = 0;
  if (disk_encoding_nibblize_track(
          disk_encoding_sector_order(disk_sector_order_dos), 0, sectors.data(),
          nibbles.data(), sync_mask.data(), &nibble_count,
          scratch.data()) != disk_err_none) {
    abort();
  }
  medium_cells.assign(max_track_bits / 8, 0);
  uint32_t cell_count = 0;
  if (disk_encoding_nibbles_to_bits(
          nibbles.data(), nibble_count, sync_mask.data(), medium_cells.data(),
          max_track_bits, &cell_count) != disk_err_none ||
      cell_count != medium_cell_count) {
    abort();
  }
  medium_cells.resize(medium_cell_count / 8);
}

auto medium_probe(const uint8_t*, size_t, uint32_t, const char*)
    -> DiskProbe {
  return disk_probe_definite;
}

auto medium_open(const char*, uint32_t, bool, void** out_instance)
    -> DiskError {
  *out_instance = &medium_cells;
  return disk_err_none;
}

auto medium_close(void*) -> void {}

auto medium_is_write_protected(void*) -> bool { return false; }

auto medium_read(void*, uint32_t quarter_track, uint8_t* bits,
                 uint32_t max_bits, uint32_t* out_bit_count,
                 uint8_t* out_bit_timing) -> DiskError {
  *out_bit_count = 0;
  *out_bit_timing = disk_default_bit_timing;
  if (quarter_track >= 160 || medium_cell_count > max_bits) {
    return disk_err_invalid_argument;
  }
  memcpy(bits, medium_cells.data(), medium_cells.size());
  *out_bit_count = medium_cell_count;
  return disk_err_none;
}

// The card writes a whole track back through this, and the cells it hands
// over must still be the length the drive was given.
auto medium_write(void*, uint32_t, const uint8_t* bits, uint32_t bit_count)
    -> DiskError {
  if (bits == nullptr) {
    host_saw_null = true;
    return disk_err_invalid_argument;
  }
  assert(bit_count == medium_cell_count);
  memcpy(medium_cells.data(), bits, medium_cells.size());
  return disk_err_none;
}

auto medium_driver() -> const DiskFormatDriver* {
  static const DiskFormatDriver driver = {disk_format_abi_version,
                                            disk_driver_cap_write,
                                            "AAA Fuzz Medium",
                                            nullptr,
                                            medium_probe,
                                            medium_open,
                                            medium_close,
                                            medium_is_write_protected,
                                            medium_read,
                                            medium_write,
                                            nullptr};
  return &driver;
}

PeripheralIOHandler registered_read_c0 = nullptr;
PeripheralIOHandler registered_write_c0 = nullptr;

auto mock_register_io(int, PeripheralIOHandler read_c0_handler,
                      PeripheralIOHandler write_c0_handler, PeripheralIOHandler,
                      PeripheralIOHandler) -> void {
  registered_read_c0 = read_c0_handler;
  registered_write_c0 = write_c0_handler;
}

auto mock_register_cx_rom(int, const uint8_t* rom_ptr) -> void {
  host_saw_null = host_saw_null || (rom_ptr == nullptr);
}

auto mock_log(void* instance, PeripheralLogLevel, const char* fmt, ...)
    -> void {
  host_saw_null =
      host_saw_null || (instance == nullptr) || (fmt == nullptr);
}

uint8_t bus_byte = 0x5A;

auto mock_read_floating_bus(uint32_t) -> uint8_t { return bus_byte; }

auto mock_notify_status(int slot) -> void {
  host_saw_null = host_saw_null || (slot != card_slot);
}

auto mock_notify_activity(int slot, bool) -> void {
  host_saw_null = host_saw_null || (slot != card_slot);
}

// A file the loader can stat and read a header out of; the driver above
// ignores its contents and answers with the in-memory medium.
struct MediumFile {
  char path[64] = "/tmp/linapple_fuzz_disk_ctl_XXXXXX";

  MediumFile() {
    const int fd = mkstemp(path);
    if (fd < 0) {
      abort();
    }
    const std::vector<uint8_t> junk(512, 0x17);
    const ssize_t written = write(fd, junk.data(), junk.size());
    static_cast<void>(written);
    close(fd);
  }
  ~MediumFile() { unlink(path); }

  MediumFile(const MediumFile&) = delete;
  auto operator=(const MediumFile&) -> MediumFile& = delete;
};

auto state_of(Peripheral* card, void* instance) -> DiskSavedState {
  DiskSavedState state{};
  size_t size = sizeof(state);
  if (card->save_state(instance, &state, &size) != peripheral_ok ||
      size != sizeof(state) || state.header.size != sizeof(state)) {
    abort();
  }
  return state;
}

// The head is somewhere on the medium it was given, whatever the guest did
// to the switches on the way there.
auto check_position(const DiskSavedState& state) -> void {
  for (const auto& drive : state.drives) {
    assert(drive.current_byte_pos >= 0);
    assert(static_cast<uint32_t>(drive.current_byte_pos) * 8 <
           medium_cell_count);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static MediumFile medium_file;
  static const bool medium_ready = [] {
    build_medium();
    disk_loader_register(medium_driver());
    return true;
  }();
  static_cast<void>(medium_ready);

  Peripheral* card = disk_get_descriptor();

  HostInterface host{};
  host.Log = mock_log;
  host.RegisterIO = mock_register_io;
  host.RegisterCxROM = mock_register_cx_rom;
  host.ReadFloatingBus = mock_read_floating_bus;
  host.NotifyStatusChanged = mock_notify_status;
  host.NotifyActivityChanged = mock_notify_activity;

  registered_read_c0 = nullptr;
  registered_write_c0 = nullptr;
  host_saw_null = false;

  void* instance = card->init(card_slot, &host);
  if (instance == nullptr || registered_read_c0 == nullptr ||
      registered_write_c0 == nullptr) {
    return 0;
  }

  DiskInsertCmd insert{};
  insert.drive = disk_drive_0;
  memcpy(insert.path, medium_file.path, strlen(medium_file.path) + 1);
  if (card->command(instance, disk_cmd_insert, &insert, sizeof(insert)) !=
      peripheral_ok) {
    card->shutdown(instance);
    return 0;
  }

  for (size_t offset = 0; offset + record_size <= size; offset += record_size) {
    const uint8_t* record = data + offset;
    const auto address = static_cast<uint16_t>(card_page | (record[1] & 0x0FU));
    const uint32_t cycle = static_cast<uint32_t>(record[3]) |
                           (static_cast<uint32_t>(record[4]) << 8U);
    bus_byte = record[5];

    switch (record[0] & 0x03U) {
      case 0:
      case 1: {
        const uint8_t answer =
            registered_read_c0(instance, 0, address, 0, 0, cycle);
        const DiskSavedState after = state_of(card, instance);
        // A0 low gates the 74LS323 onto the data bus, so an even offset
        // answers with the register the state file also carries.
        if ((address & 1U) == 0) {
          assert(answer == after.io_latch);
        }
        check_position(after);

        // The sync mark never moves backwards inside a slice: the same
        // access at the same cycle leaves the medium exactly where it was.
        registered_read_c0(instance, 0, address, 0, 0, cycle);
        const DiskSavedState again = state_of(card, instance);
        assert(again.drives[0].current_byte_pos ==
               after.drives[0].current_byte_pos);
        break;
      }
      case 2: {
        registered_write_c0(instance, 0, address, 1, record[2], cycle);
        check_position(state_of(card, instance));
        break;
      }
      default: {
        card->think(instance, cycle % max_slice_cycles);
        check_position(state_of(card, instance));
        break;
      }
    }
    assert(!host_saw_null);
  }

  card->shutdown(instance);
  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
