
#pragma once

#include <type_traits>
#include <memory>
#include <iomanip>
#include <string>

namespace sab
{

	template<typename T, typename U>
	using HandleGuard_t = std::unique_ptr<typename std::remove_pointer<T>::type, U>;
	
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
	auto HandleGuard(T handle, U deleter)
	{
		return HandleGuard_t<T, U>(handle, deleter);
	}

	
	
	std::wstring FormatLastError();
	std::wstring FormatLastError(int errorCode);

	std::wstring GetCurrentUserSidString();
	
}

#define LogLastError L"0x", std::setfill(L'0'), std::setw(8), std::right, \
	std::hex, GetLastError(), L": ", sab::FormatLastError()