
#pragma once

#include "log.h"

#include <string>
#include <utility>
#include <vector>

namespace sab
{
	struct CommandLineOption
	{
		bool isGood = false;
		
		Logger::LogLevel logLevel = Logger::LogLevel::Info;
		bool isDebug = false;
		bool isService = false;
		bool isIntallService = false;
		bool isUninstallService = false;
		std::wstring configPath;
	};

	CommandLineOption ExtractCommandLineOption(const std::vector<std::wstring>& args);

	std::vector<std::wstring> SplitCommandLine(const std::wstring& cmdline);

}
