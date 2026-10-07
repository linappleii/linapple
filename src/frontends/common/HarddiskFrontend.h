// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// The host side of the hard disk card: the slot it sits in, the two
// configuration keys that remember its images, and where a refused image is
// reported. The card itself reads and writes no key; what the drive holds
// after a command is what the configuration records.

// Told of an insert or eject the card refused: the drive, the HarddiskError_e
// code and the text harddisk_frontend_error_message gives for it. The default
// reporter writes the log; a windowed frontend points it at a dialog.
using HarddiskErrorReporter_t = auto (*)(int drive, int error,
                                         const char* message) -> void;

// No card in the machine, as the helper reports it.
constexpr int harddisk_frontend_no_card = -1;

// Run once the cards exist, and again after a restart.
auto harddisk_frontend_initialize() -> void;
// The slot holding the card, or harddisk_frontend_no_card.
auto harddisk_frontend_slot() -> int;

// Each queues its command, lets the card take it, records what the drive then
// holds under Preferences/Harddisk Image n for the next save to write, and
// returns the drive's error code: 0 for success, a HarddiskError_e for a
// refusal, which is also handed to the reporter, or harddisk_frontend_no_card
// with a log line and nothing done.
auto harddisk_frontend_insert(int drive, const char* path, bool write_protected)
    -> int;
auto harddisk_frontend_eject(int drive) -> int;

auto harddisk_frontend_error_message(int error) -> const char*;
auto harddisk_frontend_set_error_reporter(HarddiskErrorReporter_t reporter)
    -> void;
