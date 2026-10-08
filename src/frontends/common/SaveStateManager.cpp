// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/SaveStateManager.h"

#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/MouseFrontend.h"

static constexpr const char* default_snapshot_name = "SaveState.aws";

bool save_state_on_exit = false;

static std::array<char, path_max_len> s_save_state_filename{};

static auto resolve_snapshot_filename() -> const char* {
  if (s_save_state_filename[0] != '\0') {
    return s_save_state_filename.data();
  }
  return default_snapshot_name;
}

auto save_state_get_filename() -> const char* {
  return s_save_state_filename.data();
}

auto save_state_set_filename(const char* filename) -> void {
  if (filename == nullptr || *filename == '\0') {
    s_save_state_filename[0] = '\0';
    return;
  }
  util_safe_strcpy(s_save_state_filename.data(), filename,
                   s_save_state_filename.size());
}

// Legacy snapshots omit the peripheral slot trailer; accept both sizes for
// backward compatibility.
static auto snapshot_layout_size(size_t file_size) -> size_t {
  if (file_size == snapshot_size_fixed_body ||
      file_size == sizeof(Snapshot_t)) {
    return file_size;
  }
  return 0;
}

auto save_state_load() -> bool {
  auto snapshot = std::unique_ptr<Snapshot_t>(new Snapshot_t());
  const char* filename = resolve_snapshot_filename();

  FilePtr file{fopen(filename, "rb"), fclose};
  if (!file) {
    Logger::error("Failed to open save state file for reading: %s\n", filename);
    return false;
  }

  const size_t header_read =
      fread(&snapshot->hdr, 1, sizeof(snapshot->hdr), file.get());
  if (header_read != sizeof(snapshot->hdr)) {
    Logger::error("Save state file %s is shorter than its header\n", filename);
    return false;
  }

  if (snapshot->hdr.tag != static_cast<uint32_t>(snapshot_file_tag)) {
    Logger::error("Invalid save state file format or tag mismatch in %s\n",
                  filename);
    return false;
  }

  if (snapshot->hdr.version != snapshot_version) {
    Logger::error("Version mismatch in save state file %s\n", filename);
    return false;
  }

  auto* body = reinterpret_cast<uint8_t*>(snapshot.get()) + header_read;
  const size_t body_read =
      fread(body, 1, sizeof(Snapshot_t) - header_read, file.get());
  const bool at_end = (fgetc(file.get()) == EOF);
  file.reset();

  const size_t file_size = header_read + body_read;
  if (!at_end || snapshot_layout_size(file_size) == 0) {
    Logger::error(
        "Save state file %s is %s%zu bytes; a save state is %zu bytes, or %zu "
        "with the slot trailer\n",
        filename, at_end ? "" : "more than ", file_size,
        snapshot_size_fixed_body, sizeof(Snapshot_t));
    return false;
  }

  if (!snapshot_deserialize(snapshot.get())) {
    Logger::error("Failed to deserialize machine state from %s\n", filename);
    return false;
  }

  // A load may change which slot holds the mouse card.
  mouse_frontend_initialize();
  Logger::info("Loaded save state from: %s\n", filename);
  return true;
}

auto save_state_save() -> void {
  auto snapshot = std::unique_ptr<Snapshot_t>(new Snapshot_t());
  snapshot_serialize(snapshot.get());

  const char* filename = resolve_snapshot_filename();
  const std::string temp_filename = std::string(filename) + ".tmp";

  FilePtr file{fopen(temp_filename.c_str(), "wb"), fclose};
  if (!file) {
    Logger::error("Failed to open save state file for writing: %s\n",
                  temp_filename.c_str());
    return;
  }

  const size_t bytes_written =
      fwrite(snapshot.get(), 1, sizeof(Snapshot_t), file.get());
  const bool flush_ok = (fflush(file.get()) == 0);
  file.reset();

  if (bytes_written != sizeof(Snapshot_t) || !flush_ok) {
    unlink(temp_filename.c_str());
    Logger::error(
        "Failed to write complete save state data to %s (wrote %zu of %zu "
        "bytes)\n",
        filename, bytes_written, sizeof(Snapshot_t));
    return;
  }

  if (std::rename(temp_filename.c_str(), filename) != 0) {
    unlink(temp_filename.c_str());
    Logger::error("Failed to commit save state file to: %s\n", filename);
    return;
  }

  Logger::info("Saved state to: %s\n", filename);
}

auto save_state_startup() -> void {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  if (s_save_state_filename[0] != '\0') {
    save_state_load();
    return;
  }

  if (save_state_on_exit && access(default_snapshot_name, F_OK) == 0) {
    save_state_set_filename(default_snapshot_name);
    save_state_load();
  }
}

auto save_state_shutdown() -> void {
  static bool done = false;
  if (done || !save_state_on_exit) {
    return;
  }
  done = true;

  save_state_save();
}
