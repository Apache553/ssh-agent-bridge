
#include "cmdline_option.h"

#include <cwctype>

static bool EqualStringIgnoreCase(const std::wstring& a, const std::wstring& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (std::towlower(a[i]) != std::towlower(b[i]))
			return false;
	}
	return true;
}

sab::CommandLineOption sab::ExtractCommandLineOption(const std::vector<std::wstring>& args)
{
	CommandLineOption ret;
	for (size_t i = 1; i < args.size(); ++i)
	{
		auto& cur = args[i];
		if (EqualStringIgnoreCase(cur, L"/Loglevel"))
		{
			if (i + 1 >= args.size())
				return ret;
			++i;
			auto& next = args[i];
			if (EqualStringIgnoreCase(next, L"debug"))
				ret.logLevel = Logger::LogLevel::Debug;
			else if (EqualStringIgnoreCase(next, L"info"))
				ret.logLevel = Logger::LogLevel::Info;
			else if (EqualStringIgnoreCase(next, L"warn"))
				ret.logLevel = Logger::LogLevel::Warning;
			else if (EqualStringIgnoreCase(next, L"error"))
				ret.logLevel = Logger::LogLevel::Error;
			else
				return ret;
		}
		else if (EqualStringIgnoreCase(cur, L"/Service"))
		{
			ret.isService = true;
		}
		else if (EqualStringIgnoreCase(cur, L"/Debug"))
		{
			ret.isDebug = true;
		}
		else if (EqualStringIgnoreCase(cur, L"/Config"))
		{
			if (i + 1 >= args.size())
				return ret;
			++i;
			ret.configPath = args[i];
		}
		else if (EqualStringIgnoreCase(cur, L"/InstallService"))
		{
			ret.isIntallService = true;
		}
		else if (EqualStringIgnoreCase(cur, L"/UninstallService"))
		{
			ret.isUninstallService = true;
		}
		else
		{
			return ret;
		}
	}

	// validate
	int exclusiveCounter = 0;
	if (ret.isService)++exclusiveCounter;
	if (ret.isIntallService)++exclusiveCounter;
	if (ret.isUninstallService)++exclusiveCounter;
	if (exclusiveCounter > 1)
		return ret;

	ret.isGood = true;
	return ret;
}


std::vector<std::wstring> sab::SplitCommandLine(const std::wstring& cmdline)
{
	std::vector<std::wstring> ret;
	std::wstring tmp;
	bool skipSpace = true;

	for (auto ch : cmdline)
	{
		switch (ch)
		{
		case L'\"':
			skipSpace = !skipSpace;
			break;
		case L' ':
		case L'\t':
			if (skipSpace && !tmp.empty())
			{
				ret.emplace_back(std::move(tmp));
				tmp = {};
			}
			else if (!skipSpace)
			{
				tmp.push_back(ch);
			}
			break;
		default:
			tmp.push_back(ch);
		}
	}
	if (!tmp.empty())
		ret.push_back(tmp);
	return ret;
}


