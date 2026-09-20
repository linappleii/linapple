// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/peripherals/disk/Disk.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "EmbeddedRoms.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"

namespace {

namespace config {
constexpr const char* disk_image1_key = "Disk Image 1";
constexpr const char* disk_image2_key = "Disk Image 2";
constexpr size_t path_max_len = disk_path_max;
}  // namespace config

namespace physical {
// The 556 on the interface card holds the drive enabled after the software
// drops the motor switch: half D2 charges C2, 22 uF, through R5, 47 kohm,
// and releases at two thirds of Vcc. A monostable times out at RC * ln 3,
// so 47e3 * 22e-6 * 1.0986 is 1.136 s, which is 1,159,235 cycles of the
// 6502 clock at 1,020,484 Hz. Sather gives the window as about a second.
constexpr uint32_t motor_off_delay_cycles = 1159235;

// The write light is a front-panel indicator rather than a hardware line,
// held for a second after the last write so a burst of them reads as one.
constexpr uint32_t write_light_cycles = 1020484;
constexpr uint8_t latch_bit = 0x80;
constexpr uint32_t units_per_step = 4;
constexpr uint32_t steps_per_cycle = 2;
constexpr uint32_t noise_seed_start = 0x5D1A3B7FU;
constexpr uint32_t noise_multiplier = 1103515245U;
constexpr uint32_t noise_increment = 12345U;
constexpr uint32_t noise_one_in_256 = 77U;
constexpr uint8_t max_reliable_zero_cells = 3;
constexpr uint32_t head_settling_cells = 7;
constexpr uint32_t stepper_magnets = 4;
constexpr int32_t quarter_tracks_per_disk = max_disk_phases * 2;
constexpr uint32_t magnet_settle_cycles = 10;
constexpr uint32_t cells_per_byte = 8;
constexpr uint32_t track_bit_bytes = max_track_bits / 8;
}  // namespace physical

namespace regs {
constexpr uint8_t addr_mask = 0x0F;
constexpr uint8_t addr_hi_mask = 0xFF;
constexpr uint16_t phase_mask = 0x0F;
}  // namespace regs

auto copy_string_to_buffer(const std::string& src, char* dest, size_t capacity)
    -> void {
  if (dest == nullptr || capacity == 0) {
    return;
  }
  const size_t copy_len = std::min(src.size(), capacity - 1);
  std::memcpy(dest, src.data(), copy_len);
  dest[copy_len] = '\0';
}

auto path_basename(const std::string& path) -> std::string {
  const size_t last_separator = path.find_last_of("/\\");
  return (last_separator != std::string::npos) ? path.substr(last_separator + 1)
                                               : path;
}

struct Disk_t {
  std::string full_path{};
  uint32_t quarter_track = 0;
  uint32_t bit_position = 0;
  int32_t cell_remaining = 0;
  uint8_t zero_cell_run = 0;
  uint32_t bit_count = 0;
  uint8_t bit_timing = disk_default_bit_timing;
  bool is_user_write_protected = false;
  bool is_data_loaded = false;
  bool is_dirty = false;
  uint32_t motor_enable_cycles = 0;
  uint32_t write_light_cycles = 0;
  std::vector<uint8_t> track_bits{};
  const DiskFormatDriver_t* driver = nullptr;
  void* driver_instance = nullptr;
  DiskError_e last_error = disk_err_none;

  Disk_t() = default;
  ~Disk_t() = default;

  Disk_t(const Disk_t&) = delete;
  auto operator=(const Disk_t&) -> Disk_t& = delete;
  Disk_t(Disk_t&&) = default;
  auto operator=(Disk_t&&) -> Disk_t& = default;
};

// The two 9334 bits that tell the P6 what to do with the data register, the
// state it is walking, and the write head's present polarity.
struct DiskSequencer_t {
  bool q6 = false;
  bool q7 = false;
  bool write_line_active = false;
  bool write_pulse_pending = false;
  uint8_t state = 0;
};

struct DiskPeripheral_t {
  // Attached physical drives
  std::array<Disk_t, disk_drive_count> drives{};
  uint16_t active_drive_index = 0;

  // Controller hardware registers and softswitch flip-flops
  uint8_t io_latch = 0;
  uint8_t last_bus_write = 0;
  uint16_t stepper_phase_mask = 0;
  bool is_motor_on = false;
  DiskSequencer_t sequencer{};

  // Rotational and timing simulation state
  uint32_t synced = 0;
  uint32_t noise_seed = 0;
  uint32_t cumulative_cycles = 0;
  uint32_t magnet_release_cycle = 0;
  uint32_t quarter_track_before_release = 0;
  bool magnet_released = false;

  HostInterface_t* host = nullptr;
  int slot = 0;

  DiskPeripheral_t() = default;
};

constexpr auto is_drive_valid(int drive_index) noexcept -> bool {
  return (drive_index >= 0 && drive_index < disk_drive_count);
}

auto get_active_drive(DiskPeripheral_t* dp) -> Disk_t& {
  const size_t index = (dp->active_drive_index < disk_drive_count)
                           ? static_cast<size_t>(dp->active_drive_index)
                           : 0;
  return dp->drives[index];
}

// The 9334 latches the phase bits, but the coil drivers in the drive
// are powered from the enable line the 556 holds up, so a latched bit only
// pulls on the cog while that line is asserted.
auto energised_magnets(DiskPeripheral_t* dp) -> uint16_t {
  return (get_active_drive(dp).motor_enable_cycles > 0) ? dp->stepper_phase_mask
                                                        : 0;
}

auto notify_status_changed(const DiskPeripheral_t* dp) -> void {
  if (dp != nullptr && dp->host != nullptr &&
      dp->host->NotifyStatusChanged != nullptr) {
    dp->host->NotifyStatusChanged(dp->slot);
  }
}

auto notify_activity_changed(const DiskPeripheral_t* dp, bool active) -> void {
  if (dp != nullptr && dp->host != nullptr &&
      dp->host->NotifyActivityChanged != nullptr) {
    dp->host->NotifyActivityChanged(dp->slot, active);
  }
}

// Every softswitch on the card except the data register leaves the data bus
// undriven, so the 6502 reads whatever the video scanner is fetching that
// cycle. A card with no slot and no host is not on a bus at all.
auto read_floating_bus(void* instance, uint32_t executed_cycles) -> uint8_t {
  const auto* dp = static_cast<const DiskPeripheral_t*>(instance);
  if (dp == nullptr || dp->host == nullptr ||
      dp->host->ReadFloatingBus == nullptr) {
    return 0xFF;
  }
  return dp->host->ReadFloatingBus(executed_cycles);
}

// Three layers decide whether the head may write: the user's notch on
// the drive, what the format can express, and what the driver knows about the
// medium and the file under it.
auto is_disk_write_protected(const DiskPeripheral_t* disk_peripheral,
                             int drive_index) -> bool {
  if (disk_peripheral == nullptr || !is_drive_valid(drive_index)) {
    return false;
  }

  const auto& disk =
      disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk.is_user_write_protected) {
    return true;
  }

  if (disk.driver == nullptr || disk.driver_instance == nullptr) {
    return false;
  }

  const bool can_write =
      (disk.driver->capabilities & disk_driver_cap_write) != 0;
  if (!can_write) {
    return true;
  }

  return disk.driver->is_write_protected(disk.driver_instance);
}

auto medium_cell(const Disk_t& drive, uint32_t index) -> uint32_t {
  return (drive.track_bits[index >> 3U] >> (7U - (index & 7U))) & 1U;
}

auto advance_medium(Disk_t* disk_ptr, uint32_t cells) -> void {
  if (disk_ptr->bit_count == 0) {
    return;
  }
  disk_ptr->bit_position =
      (disk_ptr->bit_position + cells) % disk_ptr->bit_count;
}

auto write_medium_byte(Disk_t* disk_ptr, uint8_t value) -> void {
  for (uint32_t mask = 0x80U; mask != 0U; mask >>= 1U) {
    const uint32_t index = disk_ptr->bit_position;
    const auto cell = static_cast<uint8_t>(0x80U >> (index & 7U));
    if ((value & mask) != 0U) {
      disk_ptr->track_bits[index >> 3U] |= cell;
    } else {
      disk_ptr->track_bits[index >> 3U] &= static_cast<uint8_t>(~cell);
    }
    advance_medium(disk_ptr, 1);
  }
}

// The v1 save state carries bytes, so the medium is read out from the index
// hole the same way the data register would read it.
auto decode_medium_bytes(const Disk_t& drive, uint8_t* out, uint32_t max_count)
    -> uint32_t {
  if (drive.bit_count < physical::cells_per_byte) {
    return 0;
  }

  uint32_t cell = 0;
  uint32_t written = 0;
  while (written < max_count &&
         cell <= drive.bit_count - physical::cells_per_byte) {
    while (cell < drive.bit_count && medium_cell(drive, cell) == 0) {
      ++cell;
    }
    if (cell > drive.bit_count - physical::cells_per_byte) {
      break;
    }
    uint8_t value = 0;
    for (uint32_t i = 0; i < physical::cells_per_byte; ++i) {
      value = static_cast<uint8_t>((value << 1U) | medium_cell(drive, cell++));
    }
    out[written++] = value;
  }
  return written;
}

auto encode_medium_bytes(Disk_t* disk_ptr, const uint8_t* bytes, uint32_t count)
    -> void {
  disk_ptr->bit_position = 0;
  disk_ptr->bit_count = count * physical::cells_per_byte;
  for (uint32_t i = 0; i < count; ++i) {
    write_medium_byte(disk_ptr, bytes[i]);
  }
  disk_ptr->bit_position = 0;
  disk_ptr->cell_remaining = 0;
}

auto write_track_to_driver(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (disk_peripheral == nullptr || !is_drive_valid(drive_index)) {
    return;
  }
  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk_ptr->quarter_track >=
      static_cast<uint32_t>(physical::quarter_tracks_per_disk)) {
    return;
  }

  if (is_disk_write_protected(disk_peripheral, drive_index)) {
    return;
  }

  if (disk_ptr->bit_count != 0 && disk_ptr->driver != nullptr &&
      disk_ptr->driver->write_track_bits != nullptr) {
    // A driver that refuses the track leaves the buffer dirty, because the
    // changes it holds are still not in the image.
    const DiskError_e error = disk_ptr->driver->write_track_bits(
        disk_ptr->driver_instance, disk_ptr->quarter_track,
        disk_ptr->track_bits.data(), disk_ptr->bit_count);
    disk_ptr->is_dirty = (error != disk_err_none);
  }
}

// The spindle does not stop for the head, so a track change lands the
// head at the same angle rather than at the index hole. Tracks recorded at
// different lengths hold the angle as a fraction of a revolution, and the
// arm needs a few cells to stop ringing before the amplifier can lock -
// AppleWin measured seven against Balance of Power, which reads one track
// to find its place on the next.
auto rescale_head_angle(Disk_t* disk_ptr, uint32_t previous_position,
                        uint32_t previous_count) -> void {
  if (disk_ptr->bit_count == 0) {
    disk_ptr->bit_position = 0;
    return;
  }
  if (previous_count == 0) {
    return;
  }

  const uint64_t scaled =
      (static_cast<uint64_t>(previous_position) * disk_ptr->bit_count) /
      previous_count;
  disk_ptr->bit_position = static_cast<uint32_t>(
      (scaled + physical::head_settling_cells) % disk_ptr->bit_count);
}

auto read_track_from_driver(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (disk_peripheral == nullptr || !is_drive_valid(drive_index)) {
    return;
  }

  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  const uint32_t previous_position = disk_ptr->bit_position;
  const uint32_t previous_count = disk_ptr->bit_count;

  disk_ptr->bit_position = 0;
  disk_ptr->cell_remaining = 0;
  disk_ptr->zero_cell_run = 0;
  disk_ptr->bit_count = 0;
  disk_ptr->bit_timing = disk_default_bit_timing;

  if (disk_ptr->quarter_track >=
      static_cast<uint32_t>(physical::quarter_tracks_per_disk)) {
    disk_ptr->is_data_loaded = false;
    return;
  }

  if (disk_ptr->track_bits.size() < physical::track_bit_bytes) {
    disk_ptr->track_bits.resize(physical::track_bit_bytes, 0);
  }

  if (disk_ptr->driver != nullptr) {
    uint32_t loaded_bits = 0;
    uint8_t loaded_timing = disk_default_bit_timing;
    const DiskError_e error = disk_ptr->driver->read_track_bits(
        disk_ptr->driver_instance, disk_ptr->quarter_track,
        disk_ptr->track_bits.data(), max_track_bits, &loaded_bits,
        &loaded_timing);

    disk_ptr->bit_count = (error == disk_err_none) ? loaded_bits : 0;
    disk_ptr->bit_timing = loaded_timing;
    disk_ptr->is_data_loaded = (disk_ptr->bit_count != 0);
  }

  rescale_head_angle(disk_ptr, previous_position, previous_count);
}

auto close_format_driver(Disk_t* disk_ptr) -> void {
  if (disk_ptr == nullptr || disk_ptr->driver == nullptr) {
    return;
  }
  if (disk_ptr->driver->close != nullptr &&
      disk_ptr->driver_instance != nullptr) {
    disk_ptr->driver->close(disk_ptr->driver_instance);
  }
  disk_ptr->driver = nullptr;
  disk_ptr->driver_instance = nullptr;
}

auto eject_disk_from_drive(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (disk_peripheral == nullptr || !is_drive_valid(drive_index)) {
    return;
  }

  auto& disk = disk_peripheral->drives.at(static_cast<size_t>(drive_index));
  if (disk.driver == nullptr) {
    disk = Disk_t();
    return;
  }

  if (disk.is_dirty) {
    write_track_to_driver(disk_peripheral, drive_index);
  }

  close_format_driver(&disk);

  notify_status_changed(disk_peripheral);

  disk = Disk_t();
}

auto settle_head(DiskPeripheral_t* disk_peripheral) -> void;

auto sync_drive_motor_state(DiskPeripheral_t* disk_peripheral) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }
  auto& drive = get_active_drive(disk_peripheral);
  const bool was_spinning = (drive.motor_enable_cycles > 0);
  if (disk_peripheral->is_motor_on) {
    drive.motor_enable_cycles = physical::motor_off_delay_cycles;
  }
  const bool now_spinning = (drive.motor_enable_cycles > 0);

  // The enable coming up powers the coil drivers, so whatever phase bits
  // the 9334 held while the drive was dark energise at once and pull the cog.
  if (!was_spinning && now_spinning) {
    settle_head(disk_peripheral);
  }

  if (was_spinning != now_spinning) {
    notify_activity_changed(disk_peripheral, now_spinning);
    notify_status_changed(disk_peripheral);
  }
}

auto insert_disk_into_drive(DiskPeripheral_t* disk_peripheral, int drive_index,
                            const char* image_path, bool write_protected)
    -> DiskError_e {
  if (disk_peripheral == nullptr || image_path == nullptr ||
      !is_drive_valid(drive_index)) {
    return disk_err_io;
  }
  auto& drive = disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (drive.driver != nullptr) {
    eject_disk_from_drive(disk_peripheral, drive_index);
  }
  drive = Disk_t();

  drive.is_user_write_protected = write_protected;
  const DiskError_e error =
      disk_loader_open(image_path, &drive.driver, &drive.driver_instance);

  drive.last_error = error;

  if (error != disk_err_none) {
    notify_status_changed(disk_peripheral);
    return error;
  }

  drive.full_path = image_path;

  notify_status_changed(disk_peripheral);

  return error;
}

auto disk_io_control_motor(void* instance, uint16_t, uint16_t memory_address,
                           uint8_t, uint8_t, uint32_t executed_cycles)
    -> uint8_t {
  if (instance == nullptr) {
    return read_floating_bus(instance, executed_cycles);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->is_motor_on = (memory_address & 0x01) != 0;

  // DRIVES OFF leaves the 9334's phase bits latched; only RESET' clears
  // them. The coil drivers run off the 556's enable, so the magnets hold for
  // the motor-off delay and then drop together when it lapses.
  sync_drive_motor_state(disk_peripheral);

  return read_floating_bus(instance, executed_cycles);
}

// A track change is where the image gets the chance to take what the
// head wrote, because the buffer only holds one track at a time.
auto move_head_to(DiskPeripheral_t* disk_peripheral, uint32_t quarter_track)
    -> void {
  auto& drive = get_active_drive(disk_peripheral);
  if (quarter_track == drive.quarter_track) {
    return;
  }
  if (drive.is_dirty) {
    write_track_to_driver(disk_peripheral, disk_peripheral->active_drive_index);
  }
  drive.quarter_track = quarter_track;
  drive.is_data_loaded = false;
}

// The cog the head rides on is pulled by whichever magnets are live.
// Each one draws it onto its own half track; the magnet directly across from
// the cog pulls it in no direction at all. Two live magnets share the cog
// between them, which is how the head comes to rest on an odd quarter track.
auto settle_head(DiskPeripheral_t* disk_peripheral) -> void {
  auto& drive = get_active_drive(disk_peripheral);

  const auto here = static_cast<int32_t>(drive.quarter_track);
  const int32_t half_track = here - (here % 2);
  const uint32_t cog_phase = (drive.quarter_track / 2U) & 0x03U;
  const uint16_t live = energised_magnets(disk_peripheral);

  int32_t sum = 0;
  int32_t magnets = 0;
  for (uint32_t magnet = 0; magnet < physical::stepper_magnets; ++magnet) {
    if ((live & (1U << magnet)) == 0) {
      continue;
    }
    const uint32_t offset = (magnet - cog_phase) & 0x03U;
    if (offset == 2U) {
      continue;
    }
    ++magnets;
    sum += half_track + (offset == 1U ? 2 : 0) - (offset == 3U ? 2 : 0);
  }

  if (magnets == 0) {
    return;
  }

  const int32_t settled = std::max<int32_t>(
      0,
      std::min<int32_t>(physical::quarter_tracks_per_disk - 1, sum / magnets));
  move_head_to(disk_peripheral, static_cast<uint32_t>(settled));
}

auto disk_io_control_stepper(void* instance, uint16_t, uint16_t memory_address,
                             uint8_t, uint8_t, uint32_t executed_cycles)
    -> uint8_t {
  if (instance == nullptr) {
    return read_floating_bus(instance, executed_cycles);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);
  auto& drive = get_active_drive(disk_peripheral);

  const int strobe_phase = (memory_address >> 1) & 0x03;
  const auto strobe_bit = static_cast<uint16_t>(1 << strobe_phase);
  const bool magnet_is_on = (memory_address & 0x01) != 0;

  if (magnet_is_on) {
    disk_peripheral->stepper_phase_mask |= strobe_bit;
  } else {
    disk_peripheral->stepper_phase_mask &= static_cast<uint16_t>(~strobe_bit);
  }

  // The magnets hang off the drive enable line, so a phase strobe with
  // the motor timer expired energises nothing and the head stays put.
  if (drive.motor_enable_cycles == 0) {
    return read_floating_bus(instance, executed_cycles);
  }

  const uint32_t before_settling = drive.quarter_track;
  settle_head(disk_peripheral);

  // A coil needs current for longer than this to shift the cog, so a
  // pair of magnets dropped in the same breath leaves the head between them
  // rather than letting it chase the one that outlived the other.
  if (magnet_is_on) {
    return read_floating_bus(instance, executed_cycles);
  }

  const bool cancels_the_pending_move =
      disk_peripheral->stepper_phase_mask == 0 &&
      disk_peripheral->magnet_released &&
      (disk_peripheral->cumulative_cycles -
       disk_peripheral->magnet_release_cycle) <= physical::magnet_settle_cycles;

  if (cancels_the_pending_move) {
    move_head_to(disk_peripheral,
                 disk_peripheral->quarter_track_before_release);
    disk_peripheral->magnet_released = false;
  } else {
    disk_peripheral->quarter_track_before_release = before_settling;
    disk_peripheral->magnet_released = true;
    disk_peripheral->magnet_release_cycle = disk_peripheral->cumulative_cycles;
  }

  return read_floating_bus(instance, executed_cycles);
}

auto disk_io_enable_drive(void* instance, uint16_t, uint16_t memory_address,
                          uint8_t, uint8_t, uint32_t executed_cycles)
    -> uint8_t {
  if (instance == nullptr) {
    return read_floating_bus(instance, executed_cycles);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  const uint16_t new_drive_index = static_cast<uint16_t>(memory_address & 0x01);
  if (new_drive_index != disk_peripheral->active_drive_index) {
    auto& inactive_drive = get_active_drive(disk_peripheral);
    if (inactive_drive.is_dirty) {
      write_track_to_driver(disk_peripheral,
                            disk_peripheral->active_drive_index);
    }
    inactive_drive.motor_enable_cycles = 0;
    inactive_drive.write_light_cycles = 0;
    disk_peripheral->active_drive_index = new_drive_index;
  }

  sync_drive_motor_state(disk_peripheral);

  return read_floating_bus(instance, executed_cycles);
}
#if ENABLE_ROM_DISK2
auto p6_opcode(uint8_t address) -> uint8_t { return g_rom_disk2_p6[address]; }
#else
// A controller built without its sequencer PROM has an empty socket: the
// state register never leaves zero and the data register never moves.
auto p6_opcode(uint8_t) -> uint8_t { return 0x08; }
#endif

// With no flux under it the MC3470 amplifies head noise, so it hands
// the sequencer a pulse on about thirty per cent of its steps, 77 in 256,
// dense enough that RWTS sees bytes instead of silence. The generator
// belongs to the card instance rather than to the process, so two cards
// never draw from one stream and a test replaying the same card sees the
// same noise every run.
auto noise_pulse(DiskPeripheral_t* disk_peripheral) -> bool {
  disk_peripheral->noise_seed =
      (disk_peripheral->noise_seed * physical::noise_multiplier) +
      physical::noise_increment;
  return ((disk_peripheral->noise_seed >> 16U) & 0xFFU) <
         physical::noise_one_in_256;
}

// The cell the write head is laying down is the one whose window is ending,
// which is the cell behind the one about to arrive.
auto commit_write_cell(DiskPeripheral_t* disk_peripheral, Disk_t* drive)
    -> void {
  const uint32_t index =
      (drive->bit_position + drive->bit_count - 1) % drive->bit_count;
  const auto mask = static_cast<uint8_t>(0x80U >> (index & 7U));
  if (disk_peripheral->sequencer.write_pulse_pending) {
    drive->track_bits[index >> 3U] |= mask;
  } else {
    drive->track_bits[index >> 3U] &= static_cast<uint8_t>(~mask);
  }
  disk_peripheral->sequencer.write_pulse_pending = false;
  drive->is_dirty = true;
}

// One 2 MHz step of medium under the head. The MC3470 answers a flux
// reversal with a single pulse, so a step that brings in more than one cell
// still reports one.
auto advance_medium_one_step(DiskPeripheral_t* disk_peripheral, Disk_t* drive,
                             bool may_write) -> bool {
  if (drive->bit_count == 0) {
    return noise_pulse(disk_peripheral);
  }

  bool pulse = false;
  drive->cell_remaining -= static_cast<int32_t>(physical::units_per_step);
  while (drive->cell_remaining < 0) {
    if (may_write) {
      commit_write_cell(disk_peripheral, drive);
    }
    if (medium_cell(*drive, drive->bit_position) != 0) {
      drive->zero_cell_run = 0;
      pulse = true;
    } else if (drive->zero_cell_run < physical::max_reliable_zero_cells) {
      ++drive->zero_cell_run;
    } else if (noise_pulse(disk_peripheral)) {
      // A run this long has no flux for the read amplifier to lock onto, so
      // what it hands over from the fourth blank cell on is its own noise.
      pulse = true;
    }
    drive->bit_position = (drive->bit_position + 1) % drive->bit_count;
    drive->cell_remaining += static_cast<int32_t>(drive->bit_timing);
  }
  return pulse;
}

// The P6 is a truth table addressed by the state it is in, the read
// pulse, the two 9334 mode bits and the data register's top bit. The byte it
// answers with carries the next state scattered over four address lines and,
// in its low nibble, what the 74LS323 must do with the byte it holds.
auto sequencer_step(DiskPeripheral_t* disk_peripheral, Disk_t* drive,
                    bool is_protected, bool may_write) -> void {
  const bool pulse = advance_medium_one_step(disk_peripheral, drive, may_write);

  const uint8_t state = disk_peripheral->sequencer.state;
  const auto address = static_cast<uint8_t>(
      ((state & 0x0EU) << 4U) | (pulse ? 0x00U : 0x10U) |
      (disk_peripheral->sequencer.q7 ? 0x08U : 0x00U) |
      (disk_peripheral->sequencer.q6 ? 0x04U : 0x00U) |
      ((disk_peripheral->io_latch & 0x80U) >> 6U) | (state & 0x01U));

  const uint8_t opcode = p6_opcode(address);

  // The write head follows the state register's top bit, so every step that
  // flips it is a flux reversal in the cell now under the head.
  if (disk_peripheral->sequencer.q7) {
    const bool state_drives_high = (state & 0x08U) != 0;
    if (state_drives_high != disk_peripheral->sequencer.write_line_active) {
      disk_peripheral->sequencer.write_line_active = state_drives_high;
      disk_peripheral->sequencer.write_pulse_pending = true;
    }
  }

  disk_peripheral->sequencer.state =
      static_cast<uint8_t>(((opcode & 0xC0U) >> 4U) | ((opcode & 0x10U) >> 3U) |
                           ((opcode & 0x20U) >> 5U));

  switch (opcode & 0x0FU) {
    case 0x8:
    case 0xC:
      break;
    case 0x9:
      disk_peripheral->io_latch =
          static_cast<uint8_t>(disk_peripheral->io_latch << 1U);
      break;
    case 0xA:
    case 0xE:
      disk_peripheral->io_latch = static_cast<uint8_t>(
          (disk_peripheral->io_latch >> 1U) | (is_protected ? 0x80U : 0x00U));
      break;
    case 0xB:
    case 0xF:
      disk_peripheral->io_latch = disk_peripheral->last_bus_write;
      break;
    case 0xD:
      disk_peripheral->io_latch =
          static_cast<uint8_t>((disk_peripheral->io_latch << 1U) | 1U);
      break;
    default:
      disk_peripheral->io_latch = 0;
      break;
  }
}

auto run_sequencer_cycles(DiskPeripheral_t* disk_peripheral, uint32_t cycles)
    -> void {
  if (cycles == 0) {
    return;
  }

  auto& drive = get_active_drive(disk_peripheral);

  // With the motor-enable line down the sequencer has no clock, so the
  // data register keeps whatever it was holding when the drive stopped.
  if (drive.motor_enable_cycles == 0) {
    return;
  }

  if (!drive.is_data_loaded && drive.driver != nullptr) {
    read_track_from_driver(disk_peripheral,
                           disk_peripheral->active_drive_index);
  }
  const bool is_protected = is_disk_write_protected(
      disk_peripheral, disk_peripheral->active_drive_index);
  const bool may_write = disk_peripheral->sequencer.q7 && !is_protected;

  const uint32_t steps = cycles * physical::steps_per_cycle;
  for (uint32_t step = 0; step < steps; ++step) {
    sequencer_step(disk_peripheral, &drive, is_protected, may_write);
  }
}

// A read answers with what the hardware holds at that instruction, so
// the medium and the sequencer are brought up to the cycle the 6502 is on
// before the access is served; the mark never moves backwards within a
// slice, and think finishes it.
auto sync_sequencer_to_cycle(DiskPeripheral_t* disk_peripheral,
                             uint32_t executed_cycles) -> void {
  if (executed_cycles <= disk_peripheral->synced) {
    return;
  }
  const uint32_t elapsed = executed_cycles - disk_peripheral->synced;
  disk_peripheral->synced = executed_cycles;
  disk_peripheral->cumulative_cycles += elapsed;
  run_sequencer_cycles(disk_peripheral, elapsed);
}

auto disk_io_mode_switch(void* instance, uint16_t, uint16_t memory_address,
                         uint8_t is_write, uint8_t data_value,
                         uint32_t executed_cycles) -> uint8_t {
  if (instance == nullptr) {
    return read_floating_bus(instance, executed_cycles);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  if (is_write != 0) {
    disk_peripheral->last_bus_write = data_value;
  }

  const uint16_t offset = memory_address & regs::addr_mask;
  const bool switch_is_set = (offset & 0x01) != 0;
  if ((offset & 0x02) == 0) {
    disk_peripheral->sequencer.q6 = switch_is_set;
  } else {
    disk_peripheral->sequencer.q7 = switch_is_set;
    if (switch_is_set) {
      auto& active_drive = get_active_drive(disk_peripheral);
      const bool was_already_writing = (active_drive.write_light_cycles > 0);
      active_drive.write_light_cycles = physical::write_light_cycles;
      if (!was_already_writing) {
        notify_status_changed(disk_peripheral);
      }
    }
  }

  return disk_peripheral->io_latch;
}

auto update_drive_physics(DiskPeripheral_t* disk_peripheral, Disk_t* disk_ptr,
                          uint32_t elapsed_cycles) -> void {
  if (disk_peripheral == nullptr || disk_ptr == nullptr) {
    return;
  }

  const bool is_active_drive = (&get_active_drive(disk_peripheral) == disk_ptr);

  if (disk_ptr->motor_enable_cycles > 0 && !disk_peripheral->is_motor_on) {
    if (elapsed_cycles >= disk_ptr->motor_enable_cycles) {
      disk_ptr->motor_enable_cycles = 0;
      // The enable lapsing cuts every coil at once, so no magnet
      // outlives another for the head to chase and there is no half-made
      // move left to take back.
      if (is_active_drive) {
        disk_peripheral->magnet_released = false;
      }
      if (disk_ptr->is_dirty) {
        const int drive_index =
            (disk_ptr == &disk_peripheral->drives.at(0)) ? 0 : 1;
        write_track_to_driver(disk_peripheral, drive_index);
      }
      notify_activity_changed(disk_peripheral, false);
      notify_status_changed(disk_peripheral);
    } else {
      disk_ptr->motor_enable_cycles -= elapsed_cycles;
    }
  }

  if (disk_peripheral->sequencer.q7 && is_active_drive &&
      disk_ptr->motor_enable_cycles > 0) {
    disk_ptr->write_light_cycles = physical::write_light_cycles;
  } else if (disk_ptr->write_light_cycles > 0) {
    if (elapsed_cycles >= disk_ptr->write_light_cycles) {
      disk_ptr->write_light_cycles = 0;
      notify_status_changed(disk_peripheral);
    } else {
      disk_ptr->write_light_cycles -= elapsed_cycles;
    }
  }
}

auto update_physical_disk_state(DiskPeripheral_t* disk_peripheral,
                                uint32_t elapsed_cycles) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }

  for (int i = 0; i < disk_drive_count; ++i) {
    update_drive_physics(disk_peripheral,
                         &disk_peripheral->drives.at(static_cast<size_t>(i)),
                         elapsed_cycles);
  }
}

auto swap_drives(DiskPeripheral_t* disk_peripheral) -> bool {
  if (disk_peripheral == nullptr) {
    return false;
  }

  if (disk_peripheral->drives.at(0).motor_enable_cycles > 0 ||
      disk_peripheral->drives.at(1).motor_enable_cycles > 0) {
    return false;
  }

  std::swap(disk_peripheral->drives.at(0), disk_peripheral->drives.at(1));

  notify_status_changed(disk_peripheral);

  return true;
}

auto initialize_peripheral(DiskPeripheral_t* disk_peripheral) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }

  for (size_t i = 0; i < disk_peripheral->drives.size(); ++i) {
    auto& drive = disk_peripheral->drives.at(i);
    if (drive.is_dirty) {
      write_track_to_driver(disk_peripheral, static_cast<int>(i));
    }
  }

  disk_peripheral->active_drive_index = 0;
  disk_peripheral->io_latch = 0;
  disk_peripheral->last_bus_write = 0;
  disk_peripheral->stepper_phase_mask = 0;
  disk_peripheral->is_motor_on = false;
  disk_peripheral->sequencer = DiskSequencer_t{};
  disk_peripheral->synced = 0;
  disk_peripheral->noise_seed = physical::noise_seed_start;
  disk_peripheral->magnet_released = false;

  for (auto& drive : disk_peripheral->drives) {
    drive.motor_enable_cycles = 0;
    drive.write_light_cycles = 0;
    drive.last_error = disk_err_none;
  }

  notify_status_changed(disk_peripheral);
}

auto get_peripheral_status(DiskPeripheral_t* disk_peripheral,
                           DiskStatus_t* status) -> void {
  if (disk_peripheral == nullptr || status == nullptr) {
    return;
  }

  *status = DiskStatus_t{};

  {
    auto& drive = disk_peripheral->drives.at(0);
    status->drive0_last_error = static_cast<int32_t>(drive.last_error);
    status->drive0_loaded = (drive.driver != nullptr) ? 1 : 0;
    status->drive0_spinning = (drive.motor_enable_cycles > 0) ? 1 : 0;
    status->drive0_writing = (drive.write_light_cycles > 0) ? 1 : 0;
    status->drive0_write_protected =
        is_disk_write_protected(disk_peripheral, 0) ? 1 : 0;
    copy_string_to_buffer(path_basename(drive.full_path), status->drive0_name,
                          disk_status_name_max);
    copy_string_to_buffer(drive.full_path, status->drive0_full_path,
                          disk_status_path_max);
  }

  {
    auto& drive = disk_peripheral->drives.at(1);
    status->drive1_last_error = static_cast<int32_t>(drive.last_error);
    status->drive1_loaded = (drive.driver != nullptr) ? 1 : 0;
    status->drive1_spinning = (drive.motor_enable_cycles > 0) ? 1 : 0;
    status->drive1_writing = (drive.write_light_cycles > 0) ? 1 : 0;
    status->drive1_write_protected =
        is_disk_write_protected(disk_peripheral, 1) ? 1 : 0;
    copy_string_to_buffer(path_basename(drive.full_path), status->drive1_name,
                          disk_status_name_max);
    copy_string_to_buffer(drive.full_path, status->drive1_full_path,
                          disk_status_path_max);
  }
}

using DiskIoHandler_t = auto (*)(void* instance, uint16_t program_counter,
                                 uint16_t memory_address, uint8_t is_write,
                                 uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t;

constexpr std::array<DiskIoHandler_t, 16> k_disk_io_handlers = {
    disk_io_control_stepper,  // 0x0: Phase 0 Off
    disk_io_control_stepper,  // 0x1: Phase 0 On
    disk_io_control_stepper,  // 0x2: Phase 1 Off
    disk_io_control_stepper,  // 0x3: Phase 1 On
    disk_io_control_stepper,  // 0x4: Phase 2 Off
    disk_io_control_stepper,  // 0x5: Phase 2 On
    disk_io_control_stepper,  // 0x6: Phase 3 Off
    disk_io_control_stepper,  // 0x7: Phase 3 On
    disk_io_control_motor,    // 0x8: motor_off
    disk_io_control_motor,    // 0x9: motor_on
    disk_io_enable_drive,     // 0xA: drive_1
    disk_io_enable_drive,     // 0xB: drive_2
    disk_io_mode_switch,      // 0xC: Q6 clear
    disk_io_mode_switch,      // 0xD: Q6 set
    disk_io_mode_switch,      // 0xE: Q7 clear
    disk_io_mode_switch       // 0xF: Q7 set
};

// A0 gates the data register onto the bus (UTAIIe Table 9.1), so what an
// access does and what it answers with are two separate questions: every
// switch still fires, but only the even offsets drive the eight data lines.
auto disk_io_read(void* instance, uint16_t program_counter,
                  uint16_t memory_address, uint8_t is_write, uint8_t,
                  uint32_t executed_cycles) -> uint8_t {
  if (instance == nullptr || is_write != 0) {
    return read_floating_bus(instance, executed_cycles);
  }
  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);
  sync_sequencer_to_cycle(disk_peripheral, executed_cycles);

  const uint16_t addr = memory_address & regs::addr_hi_mask;
  const size_t handler_index = addr & regs::addr_mask;
  k_disk_io_handlers[handler_index](instance, program_counter, addr, 0, 0,
                                    executed_cycles);

  if ((addr & 1) != 0) {
    return read_floating_bus(instance, executed_cycles);
  }
  return disk_peripheral->io_latch;
}

auto disk_io_write(void* instance, uint16_t program_counter,
                   uint16_t memory_address, uint8_t is_write,
                   uint8_t data_value, uint32_t executed_cycles) -> uint8_t {
  if (instance == nullptr || is_write == 0) {
    return 0;
  }
  sync_sequencer_to_cycle(static_cast<DiskPeripheral_t*>(instance),
                          executed_cycles);
  const uint16_t addr = memory_address & regs::addr_hi_mask;
  const size_t handler_index = addr & regs::addr_mask;
  return k_disk_io_handlers[handler_index](instance, program_counter, addr, 1,
                                           data_value, executed_cycles);
}

auto cmd_handle_insert(DiskPeripheral_t* dp, const void* data, size_t size)
    -> PeripheralStatus_t {
  if (dp == nullptr || data == nullptr || size < sizeof(DiskInsertCmd_t)) {
    return peripheral_error;
  }
  const auto* c = static_cast<const DiskInsertCmd_t*>(data);
  if (!is_drive_valid(c->drive) ||
      memchr(c->path, '\0', sizeof(c->path)) == nullptr) {
    return peripheral_error;
  }
  const DiskError_e error =
      insert_disk_into_drive(dp, c->drive, c->path, c->write_protected != 0);
  return (error == disk_err_none) ? peripheral_ok : peripheral_error;
}

auto cmd_handle_eject(DiskPeripheral_t* dp, const void* data, size_t size)
    -> PeripheralStatus_t {
  if (dp == nullptr || data == nullptr || size < sizeof(DiskEjectCmd_t)) {
    return peripheral_error;
  }
  const auto* c = static_cast<const DiskEjectCmd_t*>(data);
  if (!is_drive_valid(c->drive)) {
    return peripheral_error;
  }
  eject_disk_from_drive(dp, c->drive);
  return peripheral_ok;
}

auto cmd_handle_set_protect(DiskPeripheral_t* dp, const void* data, size_t size)
    -> PeripheralStatus_t {
  if (dp == nullptr || data == nullptr || size < sizeof(DiskSetProtectCmd_t)) {
    return peripheral_error;
  }
  const auto* c = static_cast<const DiskSetProtectCmd_t*>(data);
  if (!is_drive_valid(c->drive)) {
    return peripheral_error;
  }
  dp->drives.at(static_cast<size_t>(c->drive)).is_user_write_protected =
      (c->write_protected != 0);
  notify_status_changed(dp);
  return peripheral_ok;
}

// A command larger than the queue's payload is dropped before it reaches the
// card, which would look like a silent refusal to create anything.
static_assert(sizeof(DiskCreateImageCmd_t) <= PERIPHERAL_CMD_MAX_DATA,
              "DiskCreateImageCmd_t must fit in one queued command");

auto cmd_handle_create_image(const void* data, size_t size)
    -> PeripheralStatus_t {
  if (data == nullptr || size < sizeof(DiskCreateImageCmd_t)) {
    return peripheral_error;
  }
  const auto* c = static_cast<const DiskCreateImageCmd_t*>(data);
  if (memchr(c->path, '\0', sizeof(c->path)) == nullptr ||
      memchr(c->format_name, '\0', sizeof(c->format_name)) == nullptr) {
    return peripheral_error;
  }
  return (disk_loader_create(c->path, c->format_name) == disk_err_none)
             ? peripheral_ok
             : peripheral_error;
}

auto report_refused_driver(void* context, const char* driver_name,
                           const char* reason) -> void {
  auto* dp = static_cast<DiskPeripheral_t*>(context);
  if (dp == nullptr || dp->host == nullptr || dp->host->Log == nullptr) {
    return;
  }
  dp->host->Log(dp, log_error, "Disk II: refused format driver '%s': %s",
                driver_name, reason);
}

auto disk_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->RegisterIO == nullptr) {
    return nullptr;
  }
#if ENABLE_ROM_DISK2
  if (host->RegisterCxROM == nullptr) {
    return nullptr;
  }
#endif
  auto dp = std::unique_ptr<DiskPeripheral_t>(new DiskPeripheral_t());
  dp->host = host;
  dp->slot = slot;

  disk_loader_drain_rejections(report_refused_driver, dp.get());

  initialize_peripheral(dp.get());

  std::array<char, config::path_max_len> p1{};
  std::array<char, config::path_max_len> p2{};
  if (host->GetConfig != nullptr) {
    host->GetConfig("Slots", config::disk_image1_key, p1.data(), p1.size());
    host->GetConfig("Slots", config::disk_image2_key, p2.data(), p2.size());
  }

  if (p1.at(0) != '\0') {
    insert_disk_into_drive(dp.get(), 0, p1.data(), false);
  }
  if (p2.at(0) != '\0') {
    insert_disk_into_drive(dp.get(), 1, p2.data(), false);
  }

#if ENABLE_ROM_DISK2
  host->RegisterCxROM(slot, const_cast<uint8_t*>(g_rom_disk2));
#endif
  host->RegisterIO(slot, disk_io_read, disk_io_write, nullptr, nullptr);

  return dp.release();
}

auto disk_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  initialize_peripheral(static_cast<DiskPeripheral_t*>(instance));
}

auto disk_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  for (int i = 0; i < disk_drive_count; ++i) {
    eject_disk_from_drive(dp, i);
  }
  const std::unique_ptr<DiskPeripheral_t> cleanup(dp);
}

auto disk_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr || elapsed_cycles == 0) {
    return;
  }
  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);
  sync_sequencer_to_cycle(disk_peripheral, elapsed_cycles);
  disk_peripheral->synced = 0;
  update_physical_disk_state(disk_peripheral, elapsed_cycles);
}

auto disk_abi_command(void* instance, uint32_t cmd, const void* data,
                      size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  if (!peripheral_cmd_is_mine(cmd, PERIPHERAL_SUBSYSTEM_DISK)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }

  switch (static_cast<DiskCmd_t>(cmd)) {
    case disk_cmd_insert:
      return cmd_handle_insert(dp, data, size);
    case disk_cmd_eject:
      return cmd_handle_eject(dp, data, size);
    case disk_cmd_swap_drives:
      return swap_drives(dp) ? peripheral_ok : peripheral_error;
    case disk_cmd_set_protect:
      return cmd_handle_set_protect(dp, data, size);
    case disk_cmd_create_image:
      return cmd_handle_create_image(data, size);
    default:
      break;
  }
  return peripheral_incompatible;
}

auto disk_abi_query(void* instance, uint32_t cmd, void* data, size_t* size)
    -> PeripheralStatus_t {
  if (instance == nullptr || size == nullptr) {
    return peripheral_error;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);

  if (!peripheral_cmd_is_mine(cmd, PERIPHERAL_SUBSYSTEM_DISK)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }

  switch (cmd) {
    case disk_query_status: {
      const size_t required_size = sizeof(DiskStatus_t);
      if (data == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (*size < required_size) {
        *size = required_size;
        return peripheral_error;
      }
      get_peripheral_status(dp, static_cast<DiskStatus_t*>(data));
      *size = required_size;
      return peripheral_ok;
    }
    case disk_query_supported_extensions: {
      const size_t required_size =
          disk_loader_get_supported_extensions(nullptr, 0) + 1;
      if (data == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (*size < required_size) {
        *size = required_size;
        return peripheral_error;
      }
      disk_loader_get_supported_extensions(static_cast<char*>(data), *size);
      *size = required_size;
      return peripheral_ok;
    }
    case disk_query_format_count: {
      const size_t required_size = sizeof(uint32_t);
      if (data == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (*size < required_size) {
        *size = required_size;
        return peripheral_error;
      }
      *static_cast<uint32_t*>(data) = disk_loader_driver_count();
      *size = required_size;
      return peripheral_ok;
    }
    case disk_query_format_name: {
      const size_t required_size = sizeof(DiskFormatNameQuery_t);
      if (data == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (*size < required_size) {
        *size = required_size;
        return peripheral_error;
      }
      auto* query = static_cast<DiskFormatNameQuery_t*>(data);
      const DiskFormatDriver_t* driver = disk_loader_driver_at(query->index);
      if (driver == nullptr || driver->name == nullptr) {
        return peripheral_error;
      }
      copy_string_to_buffer(driver->name, query->name, sizeof(query->name));
      query->capabilities = driver->capabilities;
      *size = required_size;
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_incompatible;
}

// The v1 compatibility shim. Everything below, down to the end of
// disk_state_v1_read, is the only code that knows the shape of
// DiskSavedState_t; the card itself works in cells. Save files have carried
// this 13,897-byte layout since 1.x and keep carrying it until 4.0.0, so each
// place the conversion loses something says so beside the field that loses it.

static_assert(sizeof(DiskSavedState_t) == 13897,
              "the v1 disk save state is a fixed 13,897 bytes");

auto disk_state_v1_write(DiskPeripheral_t* dp, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  const size_t required_size = sizeof(DiskSavedState_t);
  if (buffer == nullptr) {
    *size = required_size;
    return peripheral_ok;
  }
  if (*size < required_size) {
    *size = required_size;
    return peripheral_error;
  }

  auto* s = static_cast<DiskSavedState_t*>(buffer);
  *s = DiskSavedState_t{};

  s->header.version = static_cast<uint32_t>(disk_state_version);
  s->header.size = sizeof(DiskSavedState_t);

  for (int i = 0; i < disk_drive_count; ++i) {
    auto& d = dp->drives.at(static_cast<size_t>(i));
    auto& ds = s->drives[i];
    copy_string_to_buffer(d.full_path, ds.full_path, sizeof(ds.full_path));
    // v1 knows half tracks, so an odd quarter track comes back rounded to
    // the half track below it when the state is read again.
    ds.track = static_cast<int32_t>(d.quarter_track / 4U);
    ds.phase = static_cast<int32_t>(d.quarter_track / 2U);

    // v1 stores a byte index. The cell position is kept as cells divided by
    // eight, so it round-trips to the eight-cell boundary below it and is a
    // true byte count only where every byte is eight cells.
    ds.current_byte_pos =
        static_cast<int32_t>(d.bit_position / physical::cells_per_byte);

    ds.user_write_protected = d.is_user_write_protected ? 1 : 0;
    // v1 counted these in 64-cycle ticks; the card counts cycles. A state
    // written here and read by a tick-counting build holds the motor
    // sixty-four times longer, and a tick-counted state read here holds it
    // sixty-four times shorter.
    ds.spinning_ticks = d.motor_enable_cycles;
    ds.write_light_ticks = d.write_light_cycles;

    // v1 carries bytes where the card carries cells, and it has nowhere to
    // put the medium's cell timing. A track the guest has part-written is
    // read out the way the data register reads it, so those changes survive;
    // a clean track is left empty, because the image still holds it and the
    // medium is re-read on the way back in.
    if (d.is_dirty) {
      ds.nibble_count = static_cast<int32_t>(decode_medium_bytes(
          d, ds.track_buffer, static_cast<uint32_t>(sizeof(ds.track_buffer))));
      ds.is_dirty = 1;
      ds.is_data_loaded = 1;
    }

    // The driver answers for the file's permissions now, so v1's own copy of
    // that answer has nothing left to say.
    ds.reserved_os_read_only = 0;
  }

  s->stepper_phase_mask = dp->stepper_phase_mask;
  s->active_drive_index = dp->active_drive_index;
  s->io_latch = dp->io_latch;
  s->is_motor_on = static_cast<uint8_t>(dp->is_motor_on ? 1 : 0);
  s->is_write_mode = static_cast<uint8_t>(dp->sequencer.q7 ? 1 : 0);

  // v1 has no room for Q6 or for the state the sequencer walks, and the
  // within-slice access mark is not state a save file can be asked to hold.
  s->reserved_tick = 0;
  s->reserved_speed = 0;

  *size = required_size;
  return peripheral_ok;
}

auto disk_state_v1_read(DiskPeripheral_t* dp, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  const size_t required_size = sizeof(DiskSavedState_t);
  if (buffer == nullptr || size < required_size) {
    return peripheral_error;
  }
  const auto* s = static_cast<const DiskSavedState_t*>(buffer);

  if (s->header.version != static_cast<uint32_t>(disk_state_version) ||
      s->header.size != required_size) {
    return peripheral_error;
  }

  dp->stepper_phase_mask = s->stepper_phase_mask & regs::phase_mask;
  dp->active_drive_index =
      (s->active_drive_index < disk_drive_count) ? s->active_drive_index : 0;
  dp->io_latch = s->io_latch;
  dp->is_motor_on = (s->is_motor_on != 0);
  dp->sequencer = DiskSequencer_t{};
  dp->sequencer.q7 = (s->is_write_mode != 0);
  dp->synced = 0;

  for (int i = 0; i < disk_drive_count; ++i) {
    const auto& ds = s->drives[i];
    eject_disk_from_drive(dp, i);
    const auto* end_it =
        std::find(ds.full_path, ds.full_path + sizeof(ds.full_path), '\0');
    const std::string safe_path(ds.full_path,
                                static_cast<size_t>(end_it - ds.full_path));

    const DiskError_e insert_err = insert_disk_into_drive(
        dp, i, safe_path.c_str(), ds.user_write_protected != 0);
    if (insert_err != disk_err_none) {
      continue;
    }

    auto& d = dp->drives.at(static_cast<size_t>(i));
    const bool is_out_of_bounds =
        ds.track < 0 || ds.track >= tracks_per_disk || ds.phase < 0 ||
        ds.phase >= max_disk_phases || ds.nibble_count < 0 ||
        ds.nibble_count > static_cast<int>(sizeof(ds.track_buffer)) ||
        ds.current_byte_pos < 0 ||
        static_cast<uint32_t>(ds.current_byte_pos) >
            static_cast<uint32_t>(sizeof(ds.track_buffer));

    if (is_out_of_bounds && dp->host != nullptr && dp->host->Log != nullptr) {
      dp->host->Log(
          dp, log_warn,
          "DiskSaveState: Clamped out-of-bounds drive %d state (track: %d, "
          "phase: %d, nibble_count: %d, pos: %d)",
          i, ds.track, ds.phase, ds.nibble_count, ds.current_byte_pos);
    }

    const int32_t safe_phase =
        (ds.phase >= 0 && ds.phase < max_disk_phases) ? ds.phase : 0;
    d.quarter_track = static_cast<uint32_t>(safe_phase) * 2U;
    d.motor_enable_cycles = ds.spinning_ticks;
    d.write_light_cycles = ds.write_light_ticks;

    // The head goes back over the track and the medium is re-read from the
    // image, which is what carries the cell timing v1 cannot. Only a track
    // the guest had part-written comes back out of the state instead.
    read_track_from_driver(dp, i);
    d.is_dirty = (ds.is_dirty != 0) && !is_out_of_bounds;
    if (d.is_dirty) {
      encode_medium_bytes(&d, ds.track_buffer,
                          static_cast<uint32_t>(ds.nibble_count));
      d.is_data_loaded = (d.bit_count != 0);
    }

    const uint32_t byte_pos =
        is_out_of_bounds ? 0U : static_cast<uint32_t>(ds.current_byte_pos);
    d.bit_position =
        (d.bit_count == 0)
            ? 0U
            : std::min(byte_pos * physical::cells_per_byte, d.bit_count - 1U);
    d.cell_remaining = 0;
  }

  notify_status_changed(dp);
  return peripheral_ok;
}

// End of the v1 compatibility shim.

auto disk_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (instance == nullptr || size == nullptr) {
    return peripheral_error;
  }
  return disk_state_v1_write(static_cast<DiskPeripheral_t*>(instance), buffer,
                             size);
}

auto disk_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  return disk_state_v1_read(static_cast<DiskPeripheral_t*>(instance), buffer,
                            size);
}

}  // namespace

static const Peripheral_t g_disk_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.disk_II",
    .name = "Disk II",
    .description = "Apple II floppy disk controller emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = disk_default_slot,
    .init = disk_abi_init,
    .reset = disk_abi_reset,
    .shutdown = disk_abi_shutdown,
    .think = disk_abi_think,
    .on_vblank = nullptr,
    .save_state = disk_abi_save_state,
    .load_state = disk_abi_load_state,
    .command = disk_abi_command,
    .query = disk_abi_query};

// peripheral_register and ActivePeripheral_t::api still take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto disk_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_disk_peripheral);
}

PERIPHERAL_REGISTER(g_disk_peripheral)
