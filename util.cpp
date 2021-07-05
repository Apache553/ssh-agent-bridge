
#include "util.h"

#include <Windows.h>

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
