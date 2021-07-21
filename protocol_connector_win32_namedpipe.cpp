
#include "log.h"
#include "util.h"
#include "protocol_connector_win32_namedpipe.h"

bool sab::WriteBufferToPipe(HANDLE handle, const void* buffer, DWORD length)
{
	DWORD writtenBytes = 0;
	if (!WriteFile(handle, buffer, length, &writtenBytes, NULL))
	{
		LogDebug(L"write to pipe ", handle, L" failed!", LogLastError);
		return false;
	}
	if (writtenBytes != length)
	{
		LogDebug(L"cannot write all data into pipe ", handle);
		return false;
	}
	return true;
}

bool sab::ReadBufferFromPipe(HANDLE handle, void* buffer, DWORD length)
{
	DWORD readBytes = 0;
	if (!ReadFile(handle, buffer, length, &readBytes, NULL))
	{
		LogDebug(L"read from pipe ", handle, L" failed!", LogLastError);
		return false;
	}
	if (readBytes != length)
	{
		LogDebug(L"cannot read enough data from pipe ", handle);
		return false;
	}
	return true;
}

HANDLE sab::Win32NamedPipeConnector::Connect(const std::wstring& path)
{
	HANDLE pipeHandle = CreateFileW(path.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	if (pipeHandle == INVALID_HANDLE_VALUE)
	{
		LogError(L"cannot open pipe \"", path, "\" ", LogLastError);
		return INVALID_HANDLE_VALUE;
	}
	return pipeHandle;
}
