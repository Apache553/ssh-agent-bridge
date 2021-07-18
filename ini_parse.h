
#pragma once

#include <fstream>
#include <map>
#include <string>

namespace sab
{
	typedef std::map<std::wstring, std::wstring> IniSection;
	typedef std::map<std::wstring, IniSection> IniFile;

	std::pair<IniFile, bool> ParseIniFile(const std::wstring& path);

	const IniSection* GetSection(const IniFile& file, const std::wstring& name);
	std::wstring GetSectionProperty(const IniSection& section, const std::wstring& name);
}