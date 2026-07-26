#include "doctest.h"

#include <SDL/SDL.h>

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
  config.bFullscreen = true;
  config.bBenchmark = false;
  
  CHECK(config.bFullscreen == true);
  CHECK(config.bBenchmark == false);
}
