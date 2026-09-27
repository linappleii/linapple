// SPDX-License-Identifier: GPL-2.0-only
#include "core/Asset.h"
#include "doctest.h"

namespace {

bool free_icon_called = false;
auto mock_free_icon() -> void { free_icon_called = true; }

}  // namespace

TEST_CASE("Asset: Lifecycle initialization and shutdown") {
  asset_quit();
  CHECK(assets == nullptr);

  bool ok = asset_init();
  REQUIRE(ok);
  REQUIRE(assets != nullptr);
  CHECK(assets->font != nullptr);
  CHECK(assets->splash != nullptr);
  CHECK(assets->icon == nullptr);

  // Set mock icon and free callback
  free_icon_called = false;
  int mock_icon = 42;
  assets->icon = &mock_icon;
  asset_set_free_icon_callback(mock_free_icon);

  // Re-initialization cleans up previous state and triggers icon free callback
  bool reinit_ok = asset_init();
  REQUIRE(reinit_ok);
  CHECK(free_icon_called);
  REQUIRE(assets != nullptr);
  CHECK(assets->icon == nullptr);
  CHECK(assets->font != nullptr);
  CHECK(assets->splash != nullptr);

  // Shutdown clears assets and invokes free callback if icon set
  free_icon_called = false;
  assets->icon = &mock_icon;
  asset_quit();
  CHECK(free_icon_called);
  CHECK(assets == nullptr);

  // Idempotent quit
  asset_quit();
  CHECK(assets == nullptr);
}
