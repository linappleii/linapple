#include "TuiTerminal.h"

#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static struct termios g_orig_termios;
static bool g_terminal_initialized = false;
static std::atomic<bool> g_resized(false);
static std::atomic<bool> g_interrupted(false);

static void SignalHandler(int sig) {
  switch (sig) {
    case SIGINT:
    case SIGTERM:
      g_interrupted = true;
      break;
    case SIGWINCH:
      g_resized = true;
      break;
    default:
      break;
  }
}

int TuiTerminal_Initialize() {
  if (g_terminal_initialized) {
    return 0;
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
  printf("\x1b[?1049h\x1b[?25l");
  fflush(stdout);

  // Set up signal handlers
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SignalHandler;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGWINCH, &sa, NULL);

  g_terminal_initialized = true;
  return 0;
}

void TuiTerminal_Shutdown() {
  if (!g_terminal_initialized) {
    return;
  }

  // Show cursor and exit alternate buffer
  printf("\x1b[?25h\x1b[?1049l");
  fflush(stdout);

  // Restore original terminal state
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);

  g_terminal_initialized = false;
}

bool TuiTerminal_WasResized() { return g_resized.load(); }

void TuiTerminal_ClearResized() { g_resized = false; }

bool TuiTerminal_IsInterrupted() { return g_interrupted.load(); }
