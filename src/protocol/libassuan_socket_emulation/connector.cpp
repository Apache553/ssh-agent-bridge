
#include "../../log.h"
#include "../../util.h"
#include "connector.h"

#include <fstream>
#include <WS2tcpip.h>

bool sab::SendBuffer(SOCKET sock, const char* buffer, int len)
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

bool sab::ReceiveBuffer(SOCKET sock, char* buffer, int len)
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

SOCKET sab::LibassuanSocketEmulationConnector::Connect(const std::wstring& path)
{
	// open target file
	std::ifstream sockFile;
	sockFile.open(path, std::ios::in | std::ios::binary);
	if (!sockFile.is_open())
	{
		LogDebug(L"cannot open socket file!");
		return INVALID_SOCKET;
	}

	int portNumber;
	sockFile >> portNumber;
	if ((portNumber < 0) || (portNumber > 65535))
	{
		LogDebug(L"invalid port number!");
		return INVALID_SOCKET;
	}

	// get nonce
	char nonce[NONCE_LENGTH];
	while (!sockFile.eof() && sockFile.peek() == '\n')
		sockFile.get();
	sockFile.read(nonce, NONCE_LENGTH);
	if (sockFile.gcount() != NONCE_LENGTH)
	{
		LogDebug("cannot read nonce!");
		return INVALID_SOCKET;
	}

	SOCKET connectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (connectSocket == INVALID_SOCKET)
	{
		LogDebug(L"create socket failed! ", LogWSALastError);
		return INVALID_SOCKET;
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
		return INVALID_SOCKET;
	}

	sockGuard.release();
	return connectSocket;
}
