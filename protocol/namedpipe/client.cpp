
#include "../../log.h"
#include "../../util.h"
#include "client.h"
#include "connector.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>


sab::Win32NamedPipeClient::Win32NamedPipeClient(const std::wstring& pipePath)
	:pipePath(pipePath)
{
	LogInfo(L"set client win32 named pipe target: ", pipePath);
}

bool sab::Win32NamedPipeClient::SendSshMessage(SshMessageEnvelope* message)
{
	HANDLE pipeHandle = Win32NamedPipeConnector::Connect(pipePath);
	if (pipeHandle == Win32NamedPipeConnector::INVALID)
	{
		return false;
	}
	auto pipeGuard = HandleGuard(pipeHandle, Win32NamedPipeConnector::Close);

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
