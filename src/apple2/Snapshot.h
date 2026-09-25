// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/SnapshotTypes.h"

auto snapshot_serialize(Snapshot_t* snapshot) -> void;
auto snapshot_deserialize(const Snapshot_t* snapshot) -> bool;
