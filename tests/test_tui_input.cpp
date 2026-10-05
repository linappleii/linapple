// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "doctest.h"
#include "frontends/tui/TuiInput.h"

namespace {

// Puts a pipe behind stdout for the span of the guard and hands back what was
// written. stdout is flushed on the way in and out so doctest's own report
// stays on the real descriptor.
class ScopedStdoutPipe_t {
 public:
  ScopedStdoutPipe_t() {
    fflush(stdout);
    REQUIRE(pipe(fds_.data()) == 0);
    saved_ = dup(STDOUT_FILENO);
    REQUIRE(saved_ >= 0);
    REQUIRE(dup2(fds_[1], STDOUT_FILENO) >= 0);
  }

  ~ScopedStdoutPipe_t() {
    restore();
    close(fds_[0]);
  }

  ScopedStdoutPipe_t(const ScopedStdoutPipe_t&) = delete;
  auto operator=(const ScopedStdoutPipe_t&) -> ScopedStdoutPipe_t& = delete;
  ScopedStdoutPipe_t(ScopedStdoutPipe_t&&) = delete;
  auto operator=(ScopedStdoutPipe_t&&) -> ScopedStdoutPipe_t& = delete;

  // Closing the write end first makes the read end report end of file.
  auto drain() -> std::string {
    restore();
    std::string out;
    std::array<char, 256> chunk{};
    while (true) {
      const ssize_t n = read(fds_[0], chunk.data(), chunk.size());
      if (n <= 0) {
        break;
      }
      out.append(chunk.data(), static_cast<size_t>(n));
    }
    return out;
  }

 private:
  auto restore() -> void {
    if (saved_ < 0) {
      return;
    }
    fflush(stdout);
    dup2(saved_, STDOUT_FILENO);
    close(saved_);
    close(fds_[1]);
    saved_ = -1;
  }

  std::array<int, 2> fds_{{-1, -1}};
  int saved_ = -1;
};

struct ModeSequence_t {
  std::string mode;
  bool enable;
};

// CSI ? Pm h and CSI ? Pm l, xterm's private mode set and reset; anything else
// in the stream is a failure the caller reports.
auto parse_mode_sequences(const std::string& text,
                          std::vector<ModeSequence_t>* out) -> bool {
  size_t i = 0;
  while (i < text.size()) {
    if (text.compare(i, 3, "\x1b[?") != 0) {
      return false;
    }
    i += 3;
    const size_t digits = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      ++i;
    }
    if (i == digits || i >= text.size() || (text[i] != 'h' && text[i] != 'l')) {
      return false;
    }
    out->push_back({text.substr(digits, i - digits), text[i] == 'h'});
    ++i;
  }
  return true;
}

}  // namespace

TEST_CASE(
    "TUI input: initialize and shutdown write only terminal mode sequences, "
    "and every mode they set is reset") {
  std::string written;
  {
    ScopedStdoutPipe_t pipe;
    tui_input_initialize();
    tui_input_shutdown();
    written = pipe.drain();
  }

  std::vector<ModeSequence_t> sequences;
  REQUIRE_MESSAGE(parse_mode_sequences(written, &sequences), written);
  for (size_t i = 0; i < sequences.size(); ++i) {
    if (!sequences[i].enable) {
      continue;
    }
    bool reset_later = false;
    for (size_t j = i + 1; j < sequences.size(); ++j) {
      if (!sequences[j].enable && sequences[j].mode == sequences[i].mode) {
        reset_later = true;
      }
    }
    CHECK_MESSAGE(reset_later, "mode " << sequences[i].mode << " left set");
  }
}
