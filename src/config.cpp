// Central class for managing user configuration files and locations
//
// Copyright (C) 2017 Greg Hedger

#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/stat.h>
#include <iostream>
#include <fcntl.h>
#include <cstdio>
#include <stdexcept>

#include "config.h"

using namespace std;

Config::Config()
{
}

std::string Config::GetUserFilePath()
{
	m_optsFilePath = GetHomePath() + USER_DIRECTORY_NAME;
	return m_optsFilePath.c_str();
}

void Config::ChangeToHomeDirectory()
{
	if(chdir(GetHomePath().c_str()))
	{
		// TODO: LOG ERROR
    cout << "Cannot switch to home directory ('" << GetHomePath().c_str() << "')" << std::endl;
	}
}

void Config::ChangeToUserDirectory()
{
	if(chdir(GetUserFilePath().c_str()))
	{

    cout << "Cannot switch to user directory ('" << GetUserFilePath().c_str() << "')" << std::endl;
	}
}

// Simple POSIX file copy
bool Config::CopyFile(std::string srcFile, std::string destFile)
{
	const int bufSize = 1024;
	bool bRet = true;
	char buf[bufSize];
	size_t size;

	std::cout << "Copying '" << srcFile.c_str() << "' to '" << destFile.c_str() << "'" << std::endl;

	// Attempt to open files
	int source = open(srcFile.c_str(), O_RDONLY, 0);
	int dest = open(destFile.c_str(), O_WRONLY | O_CREAT /*| O_TRUNC*/, 0644);

	if (source && dest)
	{
		// Copy
		while ((size = read(source, buf, bufSize)) > 0) {
			if(0 >= write(dest, buf, size)) {
				// Handle error;
				std::cout << "Error writing '" << destFile.c_str() << "' (" << size << ")" << std::endl;
				std::cout << "Source file: " << srcFile.c_str() << std::endl;
				bRet = false;
				break;
			}
		}

		// Close files
		if(source) {
			close(source);
		}
		if(dest) {
			close(dest);
		}
	} else {
		std::cout << "Error copying '" << srcFile.c_str() << "' to '" << destFile.c_str() << "'" << std::endl;
		bRet = false;
	}
	return bRet;
}

// ValidateUserDirectory
// Checks for presence of user directory structure for configuration files
bool Config::ValidateUserDirectory()
{

// GPH TOOD: Revisit with more elegant solution.
// Looks like there's an official way to copy all files in a directory
// for c++17 using filesystem::, but I just want something that's
// going to work.
static const char *files[] =
{
  "charset40.bmp",
  "font.bmp",
  "splash.bmp",
  "Master.dsk",
  "linapple.conf",
  "icon.bmp",
  ""
};

	bool bResult = false;
	struct stat buffer;
	std::string userDir = GetHomePath();
	userDir += USER_DIRECTORY_NAME;
	std::string installDir = GetInstallPath();

	// Check that the entire subtree exists
	bResult = (stat (userDir.c_str(), &buffer) == 0);
	bResult &= (stat ((userDir + CONF_DIRECTORY_NAME).c_str(), &buffer) == 0);
	bResult &= (stat ((userDir + SAVED_DIRECTORY_NAME).c_str(), &buffer) == 0);
	bResult &= (stat ((userDir + FTP_DIRECTORY_NAME).c_str(), &buffer) == 0);
	bResult &= (stat (GetUserFilePath().c_str(), &buffer) == 0);
	if (!bResult) {
		// Directory is absent.  This means we need to create it and copy over
		// defaults from the install location.
		mkdir(userDir.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		mkdir((userDir + CONF_DIRECTORY_NAME).c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		mkdir((userDir + SAVED_DIRECTORY_NAME).c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		mkdir((userDir + FTP_DIRECTORY_NAME).c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        }

        cout << "Copying Files" << std::endl;
        for( unsigned int i = 0; *files[ i ]; i++ ) {
            string src = (GetInstallPath() + files[ i ]);
            string dest = (GetUserFilePath() + files[ i ]);
            if (stat (dest.c_str(), &buffer) == 0) {
                // It's already there.
                continue;
            }
            if (!(stat (src.c_str(), &buffer) == 0)) {
                cout << "Could not stat " << src << "." << std::endl;
                cout << "Please ensure " << GetInstallPath() << " exists and contains the linapple resource files." << std::endl;
                throw std::runtime_error("could not copy resource files");
            }
            CopyFile(src, dest);
        }

	return bResult;
}

std::string Config::GetInstallPath()
{
	return INSTALL_DIRECTORY_NAME;
}

std::string Config::GetHomePath()
{
	struct passwd *pw = getpwuid(getuid());
	const char *homedir = pw->pw_dir;
	std::string path = homedir;
	return path;
}
