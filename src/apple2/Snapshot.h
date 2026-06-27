// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/SnapshotTypes.h"

auto snapshot_serialize(APPLEWIN_SNAPSHOT* snapshot) -> void;
auto snapshot_deserialize(APPLEWIN_SNAPSHOT* snapshot) -> bool;
