
#pragma once

#include <type_traits>
#include <memory>
#include <iomanip>
#include <string>

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

	template<typename T, typename U>
	HandleGuard_t<T, typename std::decay<U>::type> HandleGuard(T h, U&& d)
	{
		return { std::forward<T>(h), std::forward<U>(d) };
	}

	/// <summary>
	/// Provide a RAII wrapper for handles,
	/// The CALLER should make sure handle is valid
	/// </summary>
	/// <typeparam name="T">handle type</typeparam>
	/// <typeparam name="U">deleter type</typeparam>
	/// <param name="handle">the handle will be managed</param>
	/// <param name="deleter">deleter function</param>
	/// <returns>a unique_ptr manages handle</returns>

	std::wstring FormatLastError();
	std::wstring FormatLastError(int errorCode);

	std::wstring GetCurrentUserSidString();
	std::wstring GetHandleOwnerSid(HANDLE handle);
	bool CompareStringSid(const std::wstring& sid1, const std::wstring& sid2);

	std::string WideStringToUtf8String(const std::wstring& str);

}

#define LogLastError L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, GetLastError(), L": ", sab::FormatLastError()

#define LogWSALastError L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, WSAGetLastError(), L": ", sab::FormatLastError(WSAGetLastError())