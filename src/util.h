
#pragma once

#include <type_traits>
#include <memory>
#include <iomanip>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{

	template<typename T, typename U>
	struct HandleGuard_t
	{
		T handle;
		U deleter;
		bool released;

		HandleGuard_t(T h, U&& d)
			:handle(h), deleter(std::forward<U>(d)), released(false)
		{}

		~HandleGuard_t()
		{
			if (!released)
			{
				deleter(handle);
			}
		}

		void release()
		{
			released = true;
		}
	};

	/// <summary>
	/// Provide a RAII wrapper for handles,
	/// The CALLER should make sure handle is valid
	/// </summary>
	/// <typeparam name="T">handle type</typeparam>
	/// <typeparam name="U">deleter type</typeparam>
	/// <param name="handle">the handle will be managed</param>
	/// <param name="deleter">deleter function</param>
	/// <returns>a unique_ptr manages handle</returns>
	template<typename T, typename U>
	HandleGuard_t<T, typename std::decay<U>::type> HandleGuard(T handle, U&& deleter)
	{
		return { std::forward<T>(handle), std::forward<U>(deleter) };
	}

	std::wstring FormatLastError();
	std::wstring FormatLastError(int errorCode, HMODULE moduleHandle);

	std::wstring GetCurrentUserSidString();
	std::wstring GetHandleOwnerSid(HANDLE handle);
	bool CompareStringSid(const std::wstring& sid1, const std::wstring& sid2);

	std::string WideStringToUtf8String(const std::wstring& str);

	std::wstring ReplaceEnvironmentVariables(const std::wstring& str);

	std::wstring GetExecutablePath();
	std::wstring GetExecutableDirectory();
	std::wstring GetPathParentDirectory(const std::wstring& path);
	std::wstring GetDefaultConfigPath();

	bool CheckFileExists(const std::wstring& str);

	bool EqualStringIgnoreCase(const std::wstring& a, const std::wstring& b);

}

#define LogLastError L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, GetLastError(), L": ", sab::FormatLastError()

#define LogHRESULT(hr) L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, hr, L": ", sab::FormatLastError(hr, NULL)

#define LogHRESULTInModule(hr, hmod) L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, hr, L": ", sab::FormatLastError(hr, hmod)

#define LogWSALastError L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, WSAGetLastError(), L": ", sab::FormatLastError(WSAGetLastError(), NULL)

#define LogNtStatus(x) L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, (x), L": ", sab::FormatLastError(x, GetModuleHandleW(L"ntdll.dll"))

#define LogGUID(x) L'{', std::noshowbase, std::hex, std::setfill(L'0'), \
	std::setw(8), x.Data1, L'-', std::setw(4), x.Data2, L'-', std::setw(4), x.Data3, L'-', \
	std::setw(2), x.Data4[0], std::setw(2), x.Data4[1], L'-', \
	std::setw(2), x.Data4[2], std::setw(2), x.Data4[3], std::setw(2), x.Data4[4], \
	std::setw(2), x.Data4[5], std::setw(2), x.Data4[6], std::setw(2), x.Data4[7], L'}'