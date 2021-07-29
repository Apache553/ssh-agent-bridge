
#include "../../log.h"
#include "../../util.h"
#include "client.h"
#include "connector.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>

sab::LibassuanSocketEmulationClient::LibassuanSocketEmulationClient(const std::wstring& pipePath)
	:pipePath(pipePath)
{
}

sab::LibassuanSocketEmulationClient::~LibassuanSocketEmulationClient()
{
}

bool sab::LibassuanSocketEmulationClient::SendSshMessage(SshMessageEnvelope* message)
{
	SOCKET connectSocket = LibassuanSocketEmulationConnector::Connect(pipePath);
	if (connectSocket == LibassuanSocketEmulationConnector::INVALID)
		return false;
	auto sockGuard = HandleGuard(connectSocket, LibassuanSocketEmulationConnector::Close);

	// do communication
	uint32_t beLength;
	beLength = htonl(message->length);
	if (!SendBuffer(connectSocket, reinterpret_cast<char*>(&beLength),
		HEADER_SIZE))
	{
		LogDebug(L"cannot send length prefix!");
		return false;
	}
	if (!SendBuffer(connectSocket, reinterpret_cast<char*>(message->data.data()),
		message->length))
	{
		LogDebug(L"cannot send request data!");
		return false;
	}

	message->data.clear();

	if (!ReceiveBuffer(connectSocket, reinterpret_cast<char*>(&beLength),
		HEADER_SIZE))
	{
		LogDebug(L"cannot read length prefix!");
		return false;
	}
	message->length = ntohl(beLength);
	message->data.resize(message->length);
	if (!ReceiveBuffer(connectSocket, reinterpret_cast<char*>(message->data.data()),
		message->length))
	{
		LogDebug(L"cannot read reply data!");
		return false;
	}

	return true;
}
