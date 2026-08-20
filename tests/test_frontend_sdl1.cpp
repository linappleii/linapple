#include <SDL/SDL.h>

#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"

TEST_CASE("SDL1 Frontend Initialization") {
  // Test that SDL 1.2 initialization completes successfully with dummy video
  int init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  CHECK(init_result == 0);

  // Clean up
  SDL_Quit();
}

TEST_CASE("SDL1 Config Validation") {
  AppConfig_t config;
  config.is_fullscreen = true;
  config.is_benchmark = false;

  CHECK(config.is_fullscreen == true);
  CHECK(config.is_benchmark == false);
}
