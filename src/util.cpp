
#include "util.h"
#include "log.h"

#include <memory>
#include <cwctype>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <AclAPI.h>
#include <sddl.h>

std::wstring sab::FormatLastError()
{
	DWORD errorCode = GetLastError();
	return FormatLastError(errorCode, NULL);
}

std::wstring sab::FormatLastError(int errorCode, HMODULE moduleHandle)
{
	wchar_t* buffer = nullptr;
	std::wstring ret;
	DWORD len = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | (moduleHandle ? FORMAT_MESSAGE_FROM_HMODULE : NULL),
		moduleHandle, errorCode, 0,
		reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
	if (len == 0)return L"*Cannot Format Error Message*";
	ret = buffer;
	LocalFree(buffer);
	while (ret.back() == L'\n' || ret.back() == L'\r')
		ret.pop_back();
	return ret;
}


std::wstring sab::GetCurrentUserSidString()
{
	HANDLE processToken = GetCurrentProcessToken();
	TOKEN_USER* userTokenInfo;
	DWORD bufferSize;
	std::wstring ret;

	if ((GetTokenInformation(processToken, TokenUser, nullptr,
		0, &bufferSize) == 0) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER))
	{
		std::unique_ptr<char[]> buffer(new char[bufferSize]);
		if (GetTokenInformation(processToken, TokenUser, buffer.get()
			, bufferSize, &bufferSize) != 0) {
			wchar_t* sidString;
			userTokenInfo = reinterpret_cast<TOKEN_USER*>(buffer.get());
			if (ConvertSidToStringSidW(userTokenInfo->User.Sid, &sidString) != 0)
			{
				ret = sidString;
				LocalFree(sidString);
			}
			else
			{
				LogError(L"cannot convert sid to string! ", LogLastError);
			}
		}
		else
		{
			LogError(L"cannot get current user sid! ", LogLastError);
		}
	}
	else
	{
		LogError(L"cannot get current user sid buffer length! ", LogLastError);
	}
	return ret;
}

std::wstring sab::GetHandleOwnerSid(HANDLE handle)
{
	PSID pSid;
	PSECURITY_DESCRIPTOR pSd;
	wchar_t* sidString;
	if (GetSecurityInfo(handle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
		&pSid,
		nullptr,
		nullptr,
		nullptr,
		&pSd) != 0)
	{
		LogError(L"GetSecurityInfo failed! ", LogLastError);
		return std::wstring();
	}
	auto sdGuard = HandleGuard(pSd, LocalFree);

	if (ConvertSidToStringSidW(pSid, &sidString) == 0)
	{
		LogError(L"cannot convert sid to string! ", LogLastError);
		return std::wstring();
	}
	auto strGuard = HandleGuard(sidString, LocalFree);

	return std::wstring(sidString);
}

bool sab::CompareStringSid(const std::wstring& sid1, const std::wstring& sid2)
{
	PSID pSid1, pSid2;
	if (ConvertStringSidToSidW(sid1.c_str(), &pSid1) == 0)
	{
		LogError(L"cannot convert string sid 1 to SID! ", LogLastError);
		return false;
	}
	auto sidGuard1 = HandleGuard(pSid1, LocalFree);
	if (ConvertStringSidToSidW(sid2.c_str(), &pSid2) == 0)
	{
		LogError(L"cannot convert string sid 2 to SID! ", LogLastError);
		return false;
	}
	auto sidGuard2 = sab::HandleGuard(pSid2, LocalFree);

	return EqualSid(pSid1, pSid2);
}


std::string sab::WideStringToUtf8String(const std::wstring& str)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1,
		nullptr, 0, NULL, NULL);
	if (len == 0)
	{
		return std::string();
	}
	std::unique_ptr<char[]> buffer(new char[len]);
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1,
		buffer.get(), len, NULL, NULL);
	return std::string(buffer.get());
}

std::wstring sab::ReplaceEnvironmentVariables(const std::wstring& str)
{
	DWORD length;
	length = ExpandEnvironmentStringsW(str.c_str(), nullptr, 0);
	if (length == 0)
	{
		LogDebug(L"cannot expand environment variables! ", LogLastError);
		return std::wstring();
	}
	std::unique_ptr<wchar_t[]> buffer(new wchar_t[length]);
	ExpandEnvironmentStringsW(str.c_str(), buffer.get(), length);
	return std::wstring(buffer.get());
}

std::wstring sab::GetExecutablePath()
{
	DWORD bufferLength = MAX_PATH;
	std::unique_ptr<wchar_t[]> buffer(new wchar_t[bufferLength]);

	do
	{
		SetLastError(ERROR_SUCCESS);
		DWORD strLength = GetModuleFileNameW(NULL, buffer.get(), bufferLength);
		if (GetLastError() == ERROR_SUCCESS)
		{
			return std::wstring(buffer.get(), buffer.get() + strLength);
		}
		else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			buffer.reset();
			bufferLength += bufferLength / 2; // grow by 1.5
			buffer.reset(new wchar_t[bufferLength]);
		}
		else
		{
			return std::wstring();
		}
	} while (true);
}

std::wstring sab::GetExecutableDirectory()
{
	return GetPathParentDirectory(GetExecutablePath());
}

std::wstring sab::GetPathParentDirectory(const std::wstring& path)
{
	std::wstring ret = path;
	while (!ret.empty() && (ret.back() != L'\\' && ret.back() != L'/'))
		ret.pop_back();
	if (!ret.empty())
		ret.pop_back();
	return ret;
}

std::wstring sab::GetDefaultConfigPath()
{
	std::wstring defaultFile = ReplaceEnvironmentVariables(L"%USERPROFILE%\\ssh-agent-bridge\\ssh-agent-bridge.ini");
	if (CheckFileExists(defaultFile))
		return defaultFile;
	defaultFile = GetExecutableDirectory() + L"\\ssh-agent-bridge.ini";
	if (CheckFileExists(defaultFile))
		return defaultFile;
	return std::wstring();
}

bool sab::CheckFileExists(const std::wstring& str)
{
	WIN32_FIND_DATAW findData;
	HANDLE findHandle = FindFirstFileW(str.c_str(), &findData);
	if (findHandle == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	else
	{
		FindClose(findHandle);
		return true;
	}
}

bool sab::EqualStringIgnoreCase(const std::wstring& a, const std::wstring& b)
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

