#pragma once

// This file centralizes the configuration management (save files, opts.ini)
//
// Copyright (C) 2017 Greg Hedger

#include <string>

#define USER_DIRECTORY_NAME "/.linapple/"
#define CONF_DIRECTORY_NAME "/conf/"
#define SAVED_DIRECTORY_NAME "/saved/"
#define FTP_DIRECTORY_NAME "/ftp/"
#define INSTALL_DIRECTORY_NAME "/etc/linapple/"
#define MAX_FILENAME_LENGTH 255

class Config
{
	public:
		// Constructor/Destructor
		Config();
		~Config() {};

		void ChangeToHomeDirectory();
		void ChangeToUserDirectory();
		bool ValidateUserDirectory();
		bool CopyFile(std::string source, std::string dest);
		std::string GetUserFilePath();
	protected:
		std::string GetHomePath();
		std::string GetInstallPath();
	private:
		std::string m_optsFilePath;
};

