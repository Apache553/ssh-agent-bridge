
#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{
	class Win32NamedPipeConnector
	{
	public:
		using HandleType = HANDLE;
		static constexpr HANDLE INVALID = INVALID_HANDLE_VALUE;
		static HANDLE Connect(const std::wstring& path);
		static inline void Close(HANDLE h) { ::CloseHandle(h); }
	};

	bool WriteBufferToPipe(HANDLE handle, const void* buffer, DWORD length);
	bool ReadBufferFromPipe(HANDLE handle, void* buffer, DWORD length);
}
