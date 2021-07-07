
#include "util.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>

std::wstring sab::FormatLastError()
{
	DWORD errorCode = GetLastError();
	return FormatLastError(errorCode);
}

std::wstring sab::FormatLastError(int errorCode)
{
	wchar_t* buffer = nullptr;
	std::wstring ret;
	DWORD len = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		0, errorCode, 0,
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