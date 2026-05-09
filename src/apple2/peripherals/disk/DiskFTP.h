#pragma once
#include <array>
#include <string>

extern std::array<char, 512> g_sFTPDirListing;
/* Choose an image using FTP */
bool ChooseAnImageFTP(int sx, int sy, const std::string& ftp_dir, int slot,
                      std::string& filename, bool& isdir, size_t& index_file);
