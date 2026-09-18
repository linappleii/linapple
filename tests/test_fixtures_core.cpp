// SPDX-License-Identifier: GPL-2.0-only
#include <string>

#include "core/Registry.h"
#include "test_fixtures.h"

namespace TestFixtures {

auto load_configuration_file(const std::string& path) -> bool {
  return Configuration_t::instance().load(path);
}

}  // namespace TestFixtures
