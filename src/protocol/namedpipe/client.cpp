
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

	LogDebug(L"send request: length=", message->length, L", type=0x", std::hex,
		std::setfill(L'0'), std::setw(2), message->data[0]);

	uint32_t beLength = htonl(message->length);
	if (!WriteBufferToPipe(pipeHandle, &beLength, HEADER_SIZE) ||
		!WriteBufferToPipe(pipeHandle, message->data.data(),
			static_cast<DWORD>(message->data.size())))
	{
		LogDebug(L"send request failed! ", LogLastError);
		return false;
	}

	LogDebug(L"send request successfully, reading reply.");

	if (!ReadBufferFromPipe(pipeHandle, &beLength, HEADER_SIZE))
	{
		LogDebug(L"recv reply failed! ", LogLastError);
		return false;
	}
	uint32_t tmpLength = ntohl(beLength);
	std::vector<uint8_t> tmpData(tmpLength);
	if (!ReadBufferFromPipe(pipeHandle, tmpData.data(),
		static_cast<DWORD>(tmpLength)))
	{
		LogDebug(L"recv reply failed! ", LogLastError);
		return false;
	}
	message->length = tmpLength;
	message->data = std::move(tmpData);
	LogDebug(L"recv reply: length=", message->length, L", type=0x", std::hex,
		std::setfill(L'0'), std::setw(2), message->data[0]);
	return true;
}

sab::Win32NamedPipeClient::~Win32NamedPipeClient()
{
}
