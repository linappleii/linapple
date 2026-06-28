// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/SnapshotTypes.h"

auto snapshot_serialize(ApplewinSnapshot_t* snapshot) -> void;
auto snapshot_deserialize(ApplewinSnapshot_t* snapshot) -> bool;
