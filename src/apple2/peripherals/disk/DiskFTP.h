// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

/* Choose an image using FTP */
bool choose_an_image_ftp(int sx, int sy, const std::string& ftp_dir, int slot,
                         std::string& filename, bool& isdir,
                         size_t& index_file);
