// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/SnapshotTypes.h"

auto snapshot_serialize(Snapshot* snapshot) noexcept -> void;
auto snapshot_deserialize(const Snapshot* snapshot) -> bool;
