// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Shared between the loadable fixture and the suite that loads it. The
// identity strings stay literals in the descriptor: a const char* const is
// not usable in a constant expression under C++11, and a descriptor built at
// run time is exactly what this fixture exists to keep out of the loader.
enum { slot0_fixture_query_slot = 0x5A170000 };

#define SLOT0_FIXTURE_ID "test.slot0_fixture"
#define SLOT0_FIXTURE_NAME "Slot Zero Fixture"
