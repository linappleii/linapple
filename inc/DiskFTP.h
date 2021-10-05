#pragma once

extern char g_sFTPDirListing[512]; // name for FTP-directory listing
/* Choose an image using FTP */
bool ChooseAnImageFTP(int sx, int sy, const std::string& ftp_dir, int slot,
                      std::string& filename, bool& isdir, size_t& index_file);

