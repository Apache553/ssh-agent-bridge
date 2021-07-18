
#include "ini_parse.h"

#include <fstream>
#include <codecvt>
#include <limits>
#include <memory>
#include <sstream>
#include <cwctype>

static void TrimString(std::wstring& str)
{
	while (!str.empty() && std::iswblank(str.back()))str.pop_back();
	while (!str.empty() && std::iswblank(str.front()))str.erase(str.begin());
}

std::pair<sab::IniFile, bool> sab::ParseIniFile(const std::wstring& path)
{
	std::pair<IniFile, bool> ret;
	IniFile& ini = ret.first;
	bool& status = ret.second;
	std::wifstream file;
	std::wstring line;

	status = false;

	file.imbue(std::locale(std::locale::empty(), new std::codecvt_utf8<wchar_t>));
	file.open(path, std::ios::in);
	if (!file.is_open())
		return ret;

	std::wstring sectionName;
	IniSection currentSection;

	while (!file.eof())
	{
		switch (file.peek())
		{
		case L'[':
			std::getline(file, line);
			if ((line.front() != L'[') || (line.back() != ']'))
				return ret;
			line.erase(line.begin());
			line.pop_back();
			if (line.find_first_of(L"[]=;") != std::wstring::npos)
				return ret;
			if (sectionName.empty())
			{
				sectionName = std::move(line);
				currentSection.clear();
			}
			else
			{
				ini.emplace(std::move(sectionName), std::move(currentSection));
				sectionName = std::move(line);
				currentSection = {};
			}
			break;
		case L'\n':
		case L';':
			file.ignore(std::numeric_limits<std::streamsize>::max(), L'\n');
			break;
		default:
			if (sectionName.empty())
				return ret;
			std::getline(file, line);
			if (line.find_first_of(L"[];") != std::wstring::npos)
				return ret;
			{
				std::wstring key;
				std::wstring value;
				size_t pos = line.find(L'=');
				if (pos == std::wstring::npos)
					return ret;
				key = line.substr(0, pos);
				value = line.substr(pos + 1);
				TrimString(key);
				TrimString(value);
				for (auto& ch : key)
					ch = std::towlower(ch);
				if (!key.empty())
					currentSection.emplace(std::move(key), std::move(value));
				else
					return ret;
			}
		}
	}

	if (!sectionName.empty())
		ini.emplace(std::move(sectionName), std::move(currentSection));
	status = true;

	return ret;
}

const sab::IniSection* sab::GetSection(const IniFile& file, const std::wstring& name)
{
	if (file.find(name) == file.end())
	{
		return nullptr;
	}
	return &file.find(name)->second;
}

std::wstring sab::GetSectionProperty(const IniSection& section, const std::wstring& name)
{
	if (section.find(name) == section.end())
	{
		return std::wstring();
	}
	return section.find(name)->second;
}
