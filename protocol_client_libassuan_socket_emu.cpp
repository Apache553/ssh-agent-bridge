
#include "log.h"
#include "util.h"
#include "protocol_client_libassuan_socket_emu.h"

#include <fstream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

static bool SendBuffer(SOCKET sock, const char* buffer, int len)
{
	int sentBytes = 0;
	while (sentBytes != len)
	{
		int r = send(sock, buffer + sentBytes, len - sentBytes, 0);
		if (r == SOCKET_ERROR)
		{
			LogDebug(L"send failed! ", LogWSALastError);
			return false;
		}
		sentBytes += r;
	}
	return true;
}

static bool ReceiveBuffer(SOCKET sock, char* buffer, int len)
{
	int readBytes = 0;
	while (readBytes != len)
	{
		int r = recv(sock, buffer + readBytes, len - readBytes, 0);
		if (r == SOCKET_ERROR)
		{
			LogDebug(L"send failed! ", LogWSALastError);
			return false;
		}
		else if (r == 0)
		{
			LogDebug(L"socket closed!");
			return false;
		}
		readBytes += r;
	}
	return true;
}

sab::LibassuanSocketEmulationClient::LibassuanSocketEmulationClient(const std::wstring& pipePath)
	:pipePath(pipePath)
{
}

sab::LibassuanSocketEmulationClient::~LibassuanSocketEmulationClient()
{
}

bool sab::LibassuanSocketEmulationClient::SendSshMessage(SshMessageEnvelope* message)
{
	// open target file
	std::ifstream sockFile;
	sockFile.open(pipePath, std::ios::in | std::ios::binary);
	if (!sockFile.is_open())
	{
		LogDebug(L"cannot open socket file!");
		return false;
	}

	int portNumber;
	sockFile >> portNumber;
	if ((portNumber < 0) || (portNumber > 65535))
	{
		LogDebug(L"invalid port number!");
		return false;
	}

	// get nonce
	char nonce[NONCE_LENGTH];
	while (!sockFile.eof() && sockFile.peek() == '\n')
		sockFile.get();
	sockFile.read(nonce, NONCE_LENGTH);
	if (sockFile.gcount() != NONCE_LENGTH)
	{
		LogDebug("cannot read nonce!");
		return false;
	}

	SOCKET connectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (connectSocket == INVALID_SOCKET)
	{
		LogDebug(L"create socket failed! ", LogWSALastError);
		return false;
	}
	auto sockGuard = HandleGuard(connectSocket, closesocket);

	sockaddr_in sockAddress;
	memset(&sockAddress, 0, sizeof(sockaddr_in));
	sockAddress.sin_family = AF_INET;
	if (inet_pton(AF_INET, "127.0.0.1", &sockAddress.sin_addr) <= 0)
	{
		LogDebug(L"inet_pton failed!");
		return false;
	}
	sockAddress.sin_port = htons(portNumber);

	// connect
	if (::connect(connectSocket, reinterpret_cast<sockaddr*>(&sockAddress),
		sizeof(sockAddress)) != 0)
	{
		LogDebug(L"connect failed! ", LogWSALastError);
		return false;
	}

	// write nonce
	if (!SendBuffer(connectSocket, nonce, NONCE_LENGTH))
	{
		LogDebug(L"cannot send nonce!");
		return false;
	}

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
