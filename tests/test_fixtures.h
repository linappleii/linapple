// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <unistd.h>
#include <string>
#include <vector>

namespace TestFixtures {

inline auto get_fixture_path(const std::string& filename) -> std::string {
#ifdef TEST_FIXTURES_DIR
  std::string p = std::string(TEST_FIXTURES_DIR) + "/" + filename;
  if (access(p.c_str(), R_OK) == 0) {
    return p;
  }
#endif
  for (const auto* prefix :
       {"tests/fixtures/", "../tests/fixtures/", "../../tests/fixtures/",
        "../../../tests/fixtures/"}) {
    std::string candidate = std::string(prefix) + filename;
    if (access(candidate.c_str(), R_OK) == 0) {
      return candidate;
    }
  }
  return filename;
}

}  // namespace TestFixtures
