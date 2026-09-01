// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <string>

#include "frontends/common/FileBrowser.h"

auto draw_frame_window() -> void;

auto choose_an_image(int sx, int sy, const std::string& incoming_dir, int slot,
                     std::string& filename, bool& isdir, size_t& index_file)
    -> bool;

auto choose_image_dialog(int sx, int sy, const std::string& dir, int slot,
                         FileListGenerator_t* file_list_generator,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool;

auto choose_an_image_ftp(int sx, int sy, const std::string& ftp_dir, int slot,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool;
