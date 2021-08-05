
#pragma once

#include <fstream>
#include <map>
#include <string>

namespace sab
{
	typedef std::map<std::wstring, std::wstring> IniSection;
	typedef std::map<std::wstring, IniSection> IniFile;

	std::pair<IniFile, bool> ParseIniFile(const std::wstring& path);

	std::pair<std::wstring, bool> GetPropertyString(const IniSection& section, const std::wstring& name);
	std::pair<bool, bool> GetPropertyBoolean(const IniSection& section, const std::wstring& name);
}