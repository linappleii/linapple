// SPDX-License-Identifier: GPL-2.0-only
#include "TuiTerminal.h"

#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

static struct termios g_orig_termios;
static volatile sig_atomic_t g_terminal_initialized = 0;
static std::atomic<bool> g_resized(false);
static std::atomic<bool> g_interrupted(false);
static bool s_atexit_registered = false;

static constexpr const char* k_enter_alt_screen_hide_cursor =
    "\x1b[?1049h\x1b[?25l";
static constexpr const char* k_exit_alt_screen_show_cursor =
    "\x1b[?25h\x1b[?1049l";

static auto signal_handler(int sig) -> void {
  switch (sig) {
    case SIGINT:
    case SIGTERM:
    case SIGHUP:
    case SIGQUIT:
      g_interrupted = true;
      break;
    case SIGWINCH:
      g_resized = true;
      break;
    default:
      break;
  }
}

static auto restore_terminal_signal_safe() -> void {
  if (!g_terminal_initialized) {
    return;
  }
  static const char seq[] = "\x1b[?25h\x1b[?1049l";
  ssize_t n = write(STDOUT_FILENO, seq, sizeof(seq) - 1);
  (void)n;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
  g_terminal_initialized = 0;
}

static auto fatal_signal_handler(int sig) -> void {
  restore_terminal_signal_safe();
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(sig, &sa, nullptr);
  raise(sig);
}

}  // namespace

auto tui_terminal_initialize() -> int {
  if (g_terminal_initialized != 0) {
    return 0;
  }

  if (isatty(STDIN_FILENO) == 0) {
    return 1;
  }

  // Save current terminal state
  if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
    perror("tcgetattr");
    return 1;
  }

  // Set up raw mode
  struct termios raw = g_orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    perror("tcsetattr");
    return 1;
  }

  // Enter alternate buffer and hide cursor
  fputs(k_enter_alt_screen_hide_cursor, stdout);
  fflush(stdout);

  // Set up signal handlers
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGHUP, &sa, nullptr);
  sigaction(SIGQUIT, &sa, nullptr);
  sigaction(SIGWINCH, &sa, nullptr);

  struct sigaction sa_fatal;
  memset(&sa_fatal, 0, sizeof(sa_fatal));
  sa_fatal.sa_handler = fatal_signal_handler;
  sigemptyset(&sa_fatal.sa_mask);

  sigaction(SIGSEGV, &sa_fatal, nullptr);
  sigaction(SIGABRT, &sa_fatal, nullptr);
  sigaction(SIGBUS, &sa_fatal, nullptr);
  sigaction(SIGFPE, &sa_fatal, nullptr);
  sigaction(SIGILL, &sa_fatal, nullptr);

  if (!s_atexit_registered) {
    atexit(tui_terminal_shutdown);
    s_atexit_registered = true;
  }

  g_terminal_initialized = 1;
  return 0;
}

auto tui_terminal_shutdown() -> void {
  if (g_terminal_initialized == 0) {
    return;
  }

  // Show cursor and exit alternate buffer
  fputs(k_exit_alt_screen_show_cursor, stdout);
  fflush(stdout);

  // Restore original terminal state
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);

  g_terminal_initialized = 0;
}

auto tui_terminal_was_resized() -> bool { return g_resized.load(); }

auto tui_terminal_clear_resized() -> void { g_resized = false; }

auto tui_terminal_is_interrupted() -> bool { return g_interrupted.load(); }
