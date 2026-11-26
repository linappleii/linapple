// SPDX-License-Identifier: GPL-2.0-only
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <doctest.h>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/imgui/ImGuiApp.h"
#include "frontends/sdl3-imgui/Frame.h"
#include "frontends/sdl3-imgui/Sdl3Gui.h"

auto sdl_handle_event(SDL_Event* event) -> void;

TEST_CASE("SDL3 ImGui: content rect respects chrome insets and aspect ratio") {
  // Wide window: pillarboxed inside available height between top/bottom bars.
  const ImGuiViewportRect wide =
      imgui_compute_content_rect(1280, 720, 24.0F, 26.0F, true);
  CHECK(wide.y == doctest::Approx(24.0F));
  CHECK(wide.h == doctest::Approx(670.0F));
  const float expected_w = 670.0F * (static_cast<float>(SCREEN_WIDTH) /
                                     static_cast<float>(SCREEN_HEIGHT));
  CHECK(wide.w == doctest::Approx(expected_w));
  CHECK(wide.x == doctest::Approx((1280.0F - expected_w) * 0.5F));

  // Tall window: letterboxed inside available height.
  const ImGuiViewportRect tall =
      imgui_compute_content_rect(560, 800, 20.0F, 20.0F, true);
  CHECK(tall.x == doctest::Approx(0.0F));
  CHECK(tall.w == doctest::Approx(560.0F));
  CHECK(tall.h == doctest::Approx(384.0F));
  CHECK(tall.y == doctest::Approx(20.0F + ((760.0F - 384.0F) * 0.5F)));

  // Stretch mode fills the entire region between bars.
  const ImGuiViewportRect stretched =
      imgui_compute_content_rect(1280, 720, 24.0F, 26.0F, false);
  CHECK(stretched.x == doctest::Approx(0.0F));
  CHECK(stretched.y == doctest::Approx(24.0F));
  CHECK(stretched.w == doctest::Approx(1280.0F));
  CHECK(stretched.h == doctest::Approx(670.0F));

  // Degenerate zero-sized window returns an empty rect safely.
  const ImGuiViewportRect empty =
      imgui_compute_content_rect(0, 0, 24.0F, 26.0F, true);
  CHECK(empty.w == doctest::Approx(0.0F));
  CHECK(empty.h == doctest::Approx(0.0F));
}

TEST_CASE("SDL3 ImGui: HiDPI viewport rect scales logical coordinates") {
  // Logical window 1280x720 with 24px top bar and 26px bottom bar on a 2x
  // HiDPI display (2560x1440 physical render output).
  const ImGuiViewportRect logical_vp =
      imgui_compute_content_rect(1280, 720, 24.0F, 26.0F, true);
  const SDL_FRect logical_rect{logical_vp.x, logical_vp.y, logical_vp.w,
                               logical_vp.h};
  const SDL_FRect scaled_2x =
      sdl3_gui_scale_viewport_rect(logical_rect, 1280, 720, 2560, 1440);
  CHECK(scaled_2x.x == doctest::Approx(logical_rect.x * 2.0F));
  CHECK(scaled_2x.y == doctest::Approx(48.0F));
  CHECK(scaled_2x.w == doctest::Approx(logical_rect.w * 2.0F));
  CHECK(scaled_2x.h == doctest::Approx(1340.0F));
  // Bottom edge leaves 52 physical pixels (26 * 2) for the bottom bar.
  CHECK((scaled_2x.y + scaled_2x.h) == doctest::Approx(1440.0F - 52.0F));

  // 1.5x fractional HiDPI display (1920x1080 physical for 1280x720 logical).
  const SDL_FRect scaled_15x =
      sdl3_gui_scale_viewport_rect(logical_rect, 1280, 720, 1920, 1080);
  CHECK(scaled_15x.x == doctest::Approx(logical_rect.x * 1.5F));
  CHECK(scaled_15x.y == doctest::Approx(36.0F));
  CHECK(scaled_15x.w == doctest::Approx(logical_rect.w * 1.5F));
  CHECK(scaled_15x.h == doctest::Approx(1005.0F));

  // Degenerate zero dimensions leave logical rect untouched.
  const SDL_FRect unchanged =
      sdl3_gui_scale_viewport_rect(logical_rect, 0, 720, 2560, 1440);
  CHECK(unchanged.x == doctest::Approx(logical_rect.x));
  CHECK(unchanged.y == doctest::Approx(logical_rect.y));
  CHECK(unchanged.w == doctest::Approx(logical_rect.w));
  CHECK(unchanged.h == doctest::Approx(logical_rect.h));
}

TEST_CASE("SDL3 ImGui: Super+M toggles chrome while Alt+M passes through") {
  REQUIRE(init_sdl() == 0);
  system_state.screen_width = 560;
  system_state.screen_height = 384;
  REQUIRE(frame_create_window() == 0);

  const bool initial = imgui_app_is_visible();
  const bool initial_hotkeys = keyboard_get_hotkeys_enabled();
  imgui_app_set_visible(true);
  keyboard_set_hotkeys_enabled(true);

  // Alt+M must not toggle chrome (Alt maps to Open/Solid Apple on //e).
  SDL_Event alt_m{};
  alt_m.type = SDL_EVENT_KEY_DOWN;
  alt_m.key.key = SDLK_M;
  alt_m.key.mod = SDL_KMOD_LALT;
  alt_m.key.repeat = false;
  sdl_handle_event(&alt_m);
  CHECK(imgui_app_is_visible());

  // Super+M toggles chrome when hotkeys are enabled.
  SDL_Event super_m{};
  super_m.type = SDL_EVENT_KEY_DOWN;
  super_m.key.key = SDLK_M;
  super_m.key.mod = SDL_KMOD_LGUI;
  super_m.key.repeat = false;
  sdl_handle_event(&super_m);
  CHECK_FALSE(imgui_app_is_visible());

  // Super+M is ignored when hotkeys are disabled.
  keyboard_set_hotkeys_enabled(false);
  sdl_handle_event(&super_m);
  CHECK_FALSE(imgui_app_is_visible());

  keyboard_set_hotkeys_enabled(true);
  sdl_handle_event(&super_m);
  CHECK(imgui_app_is_visible());

  keyboard_set_hotkeys_enabled(initial_hotkeys);
  imgui_app_set_visible(initial);
  frame_destroy_window();
}
