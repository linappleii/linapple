// SPDX-License-Identifier: GPL-2.0-only
#include <string>

#include "core/Registry.h"
#include "test_fixtures.h"

namespace TestFixtures {

// Defined here rather than in test_fixtures.h because the header is also
// included by targets that deliberately do not link the core, and in
// HeadlessHarness.cpp it was unreachable to every core-linked suite that does
// not use the harness.
auto load_configuration_file(const std::string& path) -> bool {
  return Configuration_t::instance().load(path);
}

}  // namespace TestFixtures
