// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/PrinterFrontend.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "core/Log.h"
#include "core/Util_Path.h"

namespace {

constexpr int slot_count = 7;
constexpr uint8_t seven_bit_mask = 0x7F;
constexpr uint8_t carriage_return = 0x0D;

struct SlotSink_t {
  FilePtr_t file{nullptr, std::fclose};
  bool in_use = false;
  bool ready = true;
  // Overwrite means one truncation per run, at the first open; a file closed
  // by a failed write and reopened by the retry keeps what was printed.
  bool truncated = false;
};

PrinterFrontendSettings_t g_settings{};
std::string g_resolved_path;
std::array<SlotSink_t, slot_count> g_slots{};

auto slot_sink(int slot) -> SlotSink_t* {
  if (slot < 1 || slot > slot_count) {
    return nullptr;
  }
  return &g_slots.at(static_cast<size_t>(slot - 1));
}

auto expand_home(const std::string& path) -> std::string {
  if (path.empty() || path.front() != '~') {
    return path;
  }
  if (path.size() > 1 && path.at(1) != '/') {
    return path;
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return path;
  }
  return std::string(home) + path.substr(1);
}

auto resolve_path(const PrinterFrontendSettings_t& settings) -> std::string {
  std::string path = expand_home(settings.filename);
  if (path.empty()) {
    path = "Printer.txt";
  }
  if (path.front() != '/') {
    path = Path::join(settings.base_dir, path);
  }
  return path;
}

auto with_slot_suffix(const std::string& path, int slot) -> std::string {
  const std::string suffix = "-slot" + std::to_string(slot);
  const size_t last_separator = path.find_last_of('/');
  const size_t name_start =
      last_separator == std::string::npos ? 0 : last_separator + 1;
  const size_t dot = path.find_last_of('.');
  // A dot that starts the name hides a file rather than giving it an
  // extension, so the suffix goes on the end.
  if (dot == std::string::npos || dot <= name_start) {
    return path + suffix;
  }
  return path.substr(0, dot) + suffix + path.substr(dot);
}

// Returns 0 or the errno of the failed fopen; says nothing, so the caller
// decides whether this attempt is worth a log line.
auto open_file(int slot, SlotSink_t& sink) -> int {
  const std::string path = printer_frontend_output_path(slot);
  const bool truncate = !g_settings.append && !sink.truncated;
  FILE* opened = std::fopen(path.c_str(), truncate ? "wb" : "ab");
  if (opened == nullptr) {
    return errno;
  }
  sink.file.reset(opened);
  sink.truncated = true;
  return 0;
}

// A printer whose file cannot be written is a printer switched off: the slot
// reports not ready, the card parks the machine, and tick keeps trying.
auto fall_over(int slot, SlotSink_t& sink, const char* action, int error)
    -> void {
  sink.file.reset();
  sink.ready = false;
  Logger::warning(
      "cannot %s printer file %s: %s; the printer in slot %d is off until "
      "the file can be opened\n",
      action, printer_frontend_output_path(slot).c_str(), std::strerror(error),
      slot);
}

auto sink_open(void* ctx, int slot, PeripheralSinkKind_t kind) -> void {
  (void)ctx;
  SlotSink_t* sink = slot_sink(slot);
  if (sink == nullptr) {
    return;
  }
  sink->file.reset();
  sink->in_use = kind == peripheral_sink_printer;
  sink->ready = sink->in_use;
}

auto sink_write(void* ctx, int slot, uint8_t byte) -> void {
  (void)ctx;
  SlotSink_t* sink = slot_sink(slot);
  if (sink == nullptr || !sink->in_use || !sink->ready) {
    return;
  }
  if (!sink->file) {
    const int error = open_file(slot, *sink);
    if (error != 0) {
      fall_over(slot, *sink, "open", error);
      return;
    }
  }
  const uint8_t out =
      g_settings.eight_bit ? byte : static_cast<uint8_t>(byte & seven_bit_mask);
  if (std::fwrite(&out, 1, 1, sink->file.get()) != 1) {
    fall_over(slot, *sink, "write", errno);
    return;
  }
  if ((byte & seven_bit_mask) == carriage_return &&
      std::fflush(sink->file.get()) != 0) {
    fall_over(slot, *sink, "write", errno);
  }
}

auto sink_ready(void* ctx, int slot) -> bool {
  (void)ctx;
  const SlotSink_t* sink = slot_sink(slot);
  return sink != nullptr && sink->in_use && sink->ready;
}

auto sink_close(void* ctx, int slot) -> void {
  (void)ctx;
  SlotSink_t* sink = slot_sink(slot);
  if (sink == nullptr) {
    return;
  }
  sink->file.reset();
  sink->in_use = false;
}

auto sink_tick(void* ctx) -> void {
  (void)ctx;
  for (int slot = 1; slot <= slot_count; ++slot) {
    SlotSink_t& sink = *slot_sink(slot);
    if (!sink.in_use || sink.ready || open_file(slot, sink) != 0) {
      continue;
    }
    sink.ready = true;
    Logger::warning(
        "printer file %s opened; the printer in slot %d is on again\n",
        printer_frontend_output_path(slot).c_str(), slot);
  }
}

const ByteSink_t g_printer_sink = {.open = sink_open,
                                   .write = sink_write,
                                   .ready = sink_ready,
                                   .close = sink_close,
                                   .tick = sink_tick};

}  // namespace

auto printer_frontend_install(const PrinterFrontendSettings_t& settings)
    -> void {
  // The bridge closes every open slot through the outgoing sink, which on a
  // re-initialisation is this one, so the files of the previous run are
  // flushed and closed before the new run's settings replace them.
  linapple_set_byte_sink(&g_printer_sink, nullptr);
  g_settings = settings;
  g_resolved_path = resolve_path(settings);
  for (auto& sink : g_slots) {
    sink.file.reset();
    sink.in_use = false;
    sink.ready = true;
    sink.truncated = false;
  }
}

auto printer_frontend_output_path(int slot) -> std::string {
  if (g_settings.primary_slot == 0 || slot == g_settings.primary_slot) {
    return g_resolved_path;
  }
  return with_slot_suffix(g_resolved_path, slot);
}
