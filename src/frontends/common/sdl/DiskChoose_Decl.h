// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <string>

struct FileListGenerator;

auto choose_an_image(int sx, int sy, const std::string& incoming_dir, int slot,
                     std::string& filename, bool& isdir, size_t& index_file)
    -> bool;

auto choose_image_dialog(int sx, int sy, const std::string& dir, int slot,
                         FileListGenerator* file_list_generator,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool;

auto draw_frame_window() -> void;

auto disk_select(int drive) -> void;
auto disk_ftp_select_image(int drive) -> void;
auto harddisk_ui_select(int drive) -> void;
auto harddisk_ui_ftp_select(int drive) -> void;
