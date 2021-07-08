#include "log.h"
#include "util.h"
#include "protocol_client_win32_namedpipe.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>

static bool WriteBufferToPipe(HANDLE handle, const void* buffer, DWORD length)
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

static bool ReadBufferFromPipe(HANDLE handle, void* buffer, DWORD length)
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

sab::Win32NamedPipeClient::Win32NamedPipeClient(const std::wstring& pipePath)
	:pipePath(pipePath)
{
	LogInfo(L"set client win32 named pipe target: ", pipePath);
}

bool sab::Win32NamedPipeClient::SendSshMessage(SshMessageEnvelope* message)
{
	DWORD writtenBytes = 0;
	HANDLE pipeHandle = CreateFileW(pipePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);
	if (pipeHandle == INVALID_HANDLE_VALUE)
	{
		LogError(L"cannot open pipe! ", LogLastError);
		return false;
	}
	auto pipeGuard = HandleGuard(pipeHandle, CloseHandle);

	uint32_t beLength = htonl(message->length);
	if (!WriteBufferToPipe(pipeHandle, &beLength, HEADER_SIZE) ||
		!WriteBufferToPipe(pipeHandle, message->data.data(),
			static_cast<DWORD>(message->data.size())))
	{
		return false;
	}

	if (!ReadBufferFromPipe(pipeHandle, &beLength, HEADER_SIZE))
	{
		return false;
	}
	message->length = ntohl(beLength);
	message->data.resize(message->length);
	if (!ReadBufferFromPipe(pipeHandle, message->data.data(),
		static_cast<DWORD>(message->data.size())))
	{
		return false;
	}
	return true;
}

sab::Win32NamedPipeClient::~Win32NamedPipeClient()
{
}
