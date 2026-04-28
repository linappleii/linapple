#include <array>
#include <unordered_map>

#include "core/Common.h"

// NOLINTBEGIN(bugprone-branch-clone)
// Justification: The keyboard mapping logic uses large switch statements to
// translate keys. Clang-tidy incorrectly flags these as identical branches when
// the logic follows a repetitive pattern, even if returning distinct values.

#include "SDL3/SDL.h"
#include "apple2/KeyboardCommands.h"
#include "apple2/Keyboard_Maps.h"
#include "apple2/Structs.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "frontends/sdl3/Frontend.h"

// Apple II ASCII codes for non-printable key mappings
static constexpr uint8_t kA2Cr = 0x0D;
static constexpr uint8_t kA2Esc = 0x1B;
static constexpr uint8_t kA2Bs =
    0x08;  // Backspace and Left arrow both send this
static constexpr uint8_t kA2Tab = 0x09;
static constexpr uint8_t kA2Space = 0x20;
static constexpr uint8_t kA2Right = 0x15;    // ctrl-U
static constexpr uint8_t kA2UpIIe = 0x0B;    // ctrl-K (IIe only)
static constexpr uint8_t kA2DownIIe = 0x0A;  // ctrl-J (IIe only)
static constexpr uint8_t kA2Del = 0x7F;      // IIe only; II/II+ sends 0x00

// 7-bit ASCII range bounds used in Frontend_ToCoreKey
static constexpr int kAscii7BitBound = 128;
static constexpr int kAsciiPrintableMin = 32;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static const Apple2KeyboardMap_t* current_locale_map = &Map_US;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int keyboard_mapping_mode = 0;  // 0=Symbolic, 1=Positional

/**
 * @brief Initialize or update mapping based on configuration.
 */
void Frontend_UpdateKeyboardMapping() {
  std::string locale;
  if (ConfigLoadString("Keyboard", "Layout", &locale)) {
    if (locale == "French") {
      current_locale_map = &Map_FR;
    } else if (locale == "German") {
      current_locale_map = &Map_DE;
    } else {
      current_locale_map = &Map_US;
    }
  }

  uint32_t mode = 0;
  if (ConfigLoadInt("Keyboard", "Mapping Mode", &mode)) {
    keyboard_mapping_mode = static_cast<int>(mode);
  }
}

static auto SDL_ScancodeToAppleIdx(SDL_Scancode scancode) -> uint8_t {
  switch (scancode) {
    case SDL_SCANCODE_A:
      return KEYB_IDX_A;
    case SDL_SCANCODE_B:
      return KEYB_IDX_B;
    case SDL_SCANCODE_C:
      return KEYB_IDX_C;
    case SDL_SCANCODE_D:
      return KEYB_IDX_D;
    case SDL_SCANCODE_E:
      return KEYB_IDX_E;
    case SDL_SCANCODE_F:
      return KEYB_IDX_F;
    case SDL_SCANCODE_G:
      return KEYB_IDX_G;
    case SDL_SCANCODE_H:
      return KEYB_IDX_H;
    case SDL_SCANCODE_I:
      return KEYB_IDX_I;
    case SDL_SCANCODE_J:
      return KEYB_IDX_J;
    case SDL_SCANCODE_K:
      return KEYB_IDX_K;
    case SDL_SCANCODE_L:
      return KEYB_IDX_L;
    case SDL_SCANCODE_M:
      return KEYB_IDX_M;
    case SDL_SCANCODE_N:
      return KEYB_IDX_N;
    case SDL_SCANCODE_O:
      return KEYB_IDX_O;
    case SDL_SCANCODE_P:
      return KEYB_IDX_P;
    case SDL_SCANCODE_Q:
      return KEYB_IDX_Q;
    case SDL_SCANCODE_R:
      return KEYB_IDX_R;
    case SDL_SCANCODE_S:
      return KEYB_IDX_S;
    case SDL_SCANCODE_T:
      return KEYB_IDX_T;
    case SDL_SCANCODE_U:
      return KEYB_IDX_U;
    case SDL_SCANCODE_V:
      return KEYB_IDX_V;
    case SDL_SCANCODE_W:
      return KEYB_IDX_W;
    case SDL_SCANCODE_X:
      return KEYB_IDX_X;
    case SDL_SCANCODE_Y:
      return KEYB_IDX_Y;
    case SDL_SCANCODE_Z:
      return KEYB_IDX_Z;
    case SDL_SCANCODE_1:
      return KEYB_IDX_1;
    case SDL_SCANCODE_2:
      return KEYB_IDX_2;
    case SDL_SCANCODE_3:
      return KEYB_IDX_3;
    case SDL_SCANCODE_4:
      return KEYB_IDX_4;
    case SDL_SCANCODE_5:
      return KEYB_IDX_5;
    case SDL_SCANCODE_6:
      return KEYB_IDX_6;
    case SDL_SCANCODE_7:
      return KEYB_IDX_7;
    case SDL_SCANCODE_8:
      return KEYB_IDX_8;
    case SDL_SCANCODE_9:
      return KEYB_IDX_9;
    case SDL_SCANCODE_0:
      return KEYB_IDX_0;
    case SDL_SCANCODE_RETURN:
      return KEYB_IDX_RETURN;
    case SDL_SCANCODE_ESCAPE:
      return KEYB_IDX_ESCAPE;
    case SDL_SCANCODE_BACKSPACE:
      return KEYB_IDX_BACKSPACE;
    case SDL_SCANCODE_TAB:
      return KEYB_IDX_TAB;
    case SDL_SCANCODE_SPACE:
      return KEYB_IDX_SPACE;
    case SDL_SCANCODE_MINUS:
      return KEYB_IDX_MINUS;
    case SDL_SCANCODE_EQUALS:
      return KEYB_IDX_EQUALS;
    case SDL_SCANCODE_LEFTBRACKET:
      return KEYB_IDX_LEFTBRACKET;
    case SDL_SCANCODE_RIGHTBRACKET:
      return KEYB_IDX_RIGHTBRACKET;
    case SDL_SCANCODE_BACKSLASH:
      return KEYB_IDX_BACKSLASH;
    case SDL_SCANCODE_SEMICOLON:
      return KEYB_IDX_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE:
      return KEYB_IDX_APOSTROPHE;
    case SDL_SCANCODE_GRAVE:
      return KEYB_IDX_GRAVE;
    case SDL_SCANCODE_COMMA:
      return KEYB_IDX_COMMA;
    case SDL_SCANCODE_PERIOD:
      return KEYB_IDX_PERIOD;
    case SDL_SCANCODE_SLASH:
      return KEYB_IDX_SLASH;
    case SDL_SCANCODE_UP:
      return KEYB_IDX_UP;
    case SDL_SCANCODE_DOWN:
      return KEYB_IDX_DOWN;
    case SDL_SCANCODE_LEFT:
      return KEYB_IDX_LEFT;
    case SDL_SCANCODE_RIGHT:
      return KEYB_IDX_RIGHT;
    default:
      return KEYB_IDX_UNKNOWN;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): key and mod are
// semantically distinct
auto Frontend_TranslateKey(SDL_Keycode key, SDL_Keymod mod) -> uint8_t {
  bool shift = (mod & SDL_KMOD_SHIFT) != 0;
  bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
  KeyboardModifiers_t km = {};
  size_t km_sz = sizeof(km);
  Peripheral_Query(0, KEYB_QUERY_MODS, &km, &km_sz);
  bool caps = (km.caps != 0);

  uint8_t apple_code = 0;

  if (key >= 'a' && key <= 'z') {
    if (ctrl) {
      apple_code = static_cast<uint8_t>(key - 'a' + 1);
    } else if (caps || shift) {
      apple_code = static_cast<uint8_t>(key - 'a' + 'A');
    } else {
      apple_code = static_cast<uint8_t>(key);
    }
  } else if (key >= '0' && key <= '9') {
    if (shift) {
      static const std::array<uint8_t, 10> shift_nums = {
          {')', '!', '@', '#', '$', '%', '^', '&', '*', '('}};
      apple_code = shift_nums.at(static_cast<size_t>(key - '0'));
    } else {
      apple_code = static_cast<uint8_t>(key);
    }
  } else {
    switch (key) {
      case SDLK_RETURN:
        apple_code = kA2Cr;
        break;
      case SDLK_ESCAPE:
        apple_code = kA2Esc;
        break;
      case SDLK_BACKSPACE:
        apple_code = kA2Bs;
        break;
      case SDLK_TAB:
        apple_code = kA2Tab;
        break;
      case SDLK_SPACE:
        apple_code = kA2Space;
        break;
      case SDLK_LEFT:
        apple_code = kA2Bs;
        break;
      case SDLK_RIGHT:
        apple_code = kA2Right;
        break;
      case SDLK_UP:
        apple_code = IS_APPLE2() ? kA2Cr : kA2UpIIe;
        break;
      case SDLK_DOWN:
        apple_code = IS_APPLE2() ? '/' : kA2DownIIe;
        break;
      case SDLK_DELETE:
        apple_code = IS_APPLE2() ? uint8_t{0} : kA2Del;
        break;

      // Symbols
      case SDLK_GRAVE:
        apple_code = shift ? '~' : '`';
        break;
      case SDLK_MINUS:
        apple_code = shift ? '_' : '-';
        break;
      case SDLK_EQUALS:
        apple_code = shift ? '+' : '=';
        break;
      case SDLK_LEFTBRACKET:
        apple_code = shift ? '{' : '[';
        break;
      case SDLK_RIGHTBRACKET:
        apple_code = shift ? '}' : ']';
        break;
      case SDLK_BACKSLASH:
        apple_code = shift ? '|' : '\\';
        break;
      case SDLK_SEMICOLON:
        apple_code = shift ? ':' : ';';
        break;
      case SDLK_APOSTROPHE:
        apple_code = shift ? '"' : '\'';
        break;
      case SDLK_COMMA:
        apple_code = shift ? '<' : ',';
        break;
      case SDLK_PERIOD:
        apple_code = shift ? '>' : '.';
        break;
      case SDLK_SLASH:
        apple_code = shift ? '?' : '/';
        break;
      default:
        break;
    }
  }

  return apple_code;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): scancode/keycode/mod
// are semantically distinct
void Frontend_DispatchKeyEvent(uint32_t scancode, uint32_t keycode,
                               uint32_t mod, bool is_down) {
  // 1. Synchronize absolute modifier state first
  KeyboardModifiers_t mods = {
      static_cast<uint8_t>((mod & SDL_KMOD_SHIFT) ? 1 : 0),
      static_cast<uint8_t>((mod & SDL_KMOD_CTRL) ? 1 : 0),
      static_cast<uint8_t>((mod & SDL_KMOD_ALT) ? 1 : 0),
      static_cast<uint8_t>((mod & SDL_KMOD_GUI) ? 1 : 0), 0};
  Peripheral_Command(0, KEYB_CMD_SET_MODS, &mods, sizeof(mods));

  // 2. Dispatch the actual key event
  uint8_t apple_ascii = 0;

  // Safety: max KEYB_IDX value must fit within the map array bounds.
  static_assert(static_cast<int>(KEYB_IDX_UP) < static_cast<int>(KEYB_MAP_SIZE),
                "KeyboardIdx enum value exceeds map array size");

  if (keyboard_mapping_mode == 1) {  // Positional
    uint8_t idx = SDL_ScancodeToAppleIdx(static_cast<SDL_Scancode>(scancode));
    if (idx != KEYB_IDX_UNKNOWN && idx < KEYB_MAP_SIZE) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index):
      // bounds checked above
      apple_ascii = current_locale_map->map[idx];
    }
  } else {  // Symbolic
    apple_ascii = Frontend_TranslateKey(static_cast<SDL_Keycode>(keycode),
                                        static_cast<SDL_Keymod>(mod));
  }

  // The 'any-key-down' flag on the Apple II ($C010 bit 7) must be updated even
  // for unmapped keys.
  KeyboardEvent_t ev = {apple_ascii, static_cast<uint8_t>(is_down ? 1 : 0)};
  Peripheral_Command(0, KEYB_CMD_EVENT, &ev, sizeof(ev));
}

// LinAppleKey encodes both named special keys (0x100+) and raw 7-bit ASCII
// codes (1-127). The cast is well-defined for the enum's underlying int type;
// the analyzer check is overly strict.
// NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
static auto AsLinAppleKey(int v) noexcept -> LinAppleKey {
  return static_cast<LinAppleKey>(v);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto Frontend_ToCoreKey(int key, uint32_t mod) -> LinAppleKey {
  bool shift = (mod & SDL_KMOD_SHIFT) != 0;
  bool ctrl = (mod & SDL_KMOD_CTRL) != 0;

  if (key < kAscii7BitBound && key >= kAsciiPrintableMin) {
    if (ctrl && key >= 'a' && key <= 'z') {
      return AsLinAppleKey(key - 'a' + 1);
    }
    if (shift) {
      if (key >= 'a' && key <= 'z') {
        return AsLinAppleKey(key - 'a' + 'A');
      }
      if (key >= '0' && key <= '9') {
        static const std::array<uint8_t, 10> shift_nums = {
            {')', '!', '@', '#', '$', '%', '^', '&', '*', '('}};
        return AsLinAppleKey(shift_nums.at(static_cast<size_t>(key - '0')));
      }
      switch (key) {
        case SDLK_GRAVE:
          return AsLinAppleKey('~');
        case SDLK_MINUS:
          return AsLinAppleKey('-');
        case SDLK_EQUALS:
          return AsLinAppleKey('=');
        case SDLK_LEFTBRACKET:
          return AsLinAppleKey('[');
        case SDLK_RIGHTBRACKET:
          return AsLinAppleKey(']');
        case SDLK_BACKSLASH:
          return AsLinAppleKey('\\');
        case SDLK_SEMICOLON:
          return AsLinAppleKey(';');
        case SDLK_APOSTROPHE:
          return AsLinAppleKey('\'');
        case SDLK_COMMA:
          return AsLinAppleKey(',');
        case SDLK_PERIOD:
          return AsLinAppleKey('.');
        case SDLK_SLASH:
          return AsLinAppleKey('/');
        default:
          break;
      }
    }
    return AsLinAppleKey(key);
  }

  switch (key) {
    case SDLK_RETURN:
      return LINAPPLE_KEY_RETURN;
    case SDLK_ESCAPE:
      return LINAPPLE_KEY_ESCAPE;
    case SDLK_BACKSPACE:
      return LINAPPLE_KEY_BACKSPACE;
    case SDLK_TAB:
      return LINAPPLE_KEY_TAB;
    case SDLK_SPACE:
      return LINAPPLE_KEY_SPACE;
    case SDLK_UP:
      return LINAPPLE_KEY_UP;
    case SDLK_DOWN:
      return LINAPPLE_KEY_DOWN;
    case SDLK_LEFT:
      return LINAPPLE_KEY_LEFT;
    case SDLK_RIGHT:
      return LINAPPLE_KEY_RIGHT;
    case SDLK_PAGEUP:
      return LINAPPLE_KEY_PAGEUP;
    case SDLK_PAGEDOWN:
      return LINAPPLE_KEY_PAGEDOWN;
    case SDLK_HOME:
      return LINAPPLE_KEY_HOME;
    case SDLK_END:
      return LINAPPLE_KEY_END;
    case SDLK_INSERT:
      return LINAPPLE_KEY_INSERT;
    case SDLK_DELETE:
      return LINAPPLE_KEY_DELETE;
    case SDLK_F1:
      return LINAPPLE_KEY_F1;
    case SDLK_F2:
      return LINAPPLE_KEY_F2;
    case SDLK_F3:
      return LINAPPLE_KEY_F3;
    case SDLK_F4:
      return LINAPPLE_KEY_F4;
    case SDLK_F5:
      return LINAPPLE_KEY_F5;
    case SDLK_F6:
      return LINAPPLE_KEY_F6;
    case SDLK_F7:
      return LINAPPLE_KEY_F7;
    case SDLK_F8:
      return LINAPPLE_KEY_F8;
    case SDLK_F9:
      return LINAPPLE_KEY_F9;
    case SDLK_F10:
      return LINAPPLE_KEY_F10;
    case SDLK_F11:
      return LINAPPLE_KEY_F11;
    case SDLK_F12:
      return LINAPPLE_KEY_F12;
    case SDLK_KP_PLUS:
      return LINAPPLE_KEY_KP_PLUS;
    case SDLK_KP_MINUS:
      return LINAPPLE_KEY_KP_MINUS;
    case SDLK_KP_MULTIPLY:
      return LINAPPLE_KEY_KP_MULTIPLY;
    case SDLK_KP_DIVIDE:
      return LINAPPLE_KEY_KP_DIVIDE;
    case SDLK_KP_ENTER:
      return LINAPPLE_KEY_KP_ENTER;
    case SDLK_LSHIFT:
      return LINAPPLE_KEY_LSHIFT;
    case SDLK_RSHIFT:
      return LINAPPLE_KEY_RSHIFT;
    case SDLK_LCTRL:
      return LINAPPLE_KEY_LCTRL;
    case SDLK_RCTRL:
      return LINAPPLE_KEY_RCTRL;
    case SDLK_LALT:
      return LINAPPLE_KEY_LALT;
    case SDLK_RALT:
      return LINAPPLE_KEY_RALT;
    default:
      return LINAPPLE_KEY_UNKNOWN;
  }
}

auto Frontend_HandleKeyEvent(SDL_Keycode key, bool is_down) -> bool {
  switch (key) {
    case SDLK_LALT:
    case SDLK_LGUI:
      Linapple_SetAppleKey(0, is_down);
      return true;

    case SDLK_RALT:
    case SDLK_RGUI:
      Linapple_SetAppleKey(1, is_down);
      return true;

    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: {
      // Modifiers must be synchronized with the peripheral logic to ensure
      // correct behavior during complex key combinations or reset sequences.
      SDL_Keymod mod = SDL_GetModState();
      KeyboardModifiers_t mods = {
          static_cast<uint8_t>((mod & SDL_KMOD_SHIFT) ? 1 : 0),
          static_cast<uint8_t>((mod & SDL_KMOD_CTRL) ? 1 : 0),
          static_cast<uint8_t>((mod & SDL_KMOD_ALT) ? 1 : 0),
          static_cast<uint8_t>((mod & SDL_KMOD_GUI) ? 1 : 0), 0};
      Peripheral_Command(0, KEYB_CMD_SET_MODS, &mods, sizeof(mods));
      return true;
    }

    default:
      return false;
  }
}

// NOLINTEND(bugprone-branch-clone)
