
#pragma once

#include <string>

#include <WinSock2.h>

namespace sab
{
	class LibassuanSocketEmulationConnector
	{
	public:
		using HandleType = SOCKET;
		static constexpr int NONCE_LENGTH = 16;
		static constexpr SOCKET INVALID = INVALID_SOCKET;

		static SOCKET Connect(const std::wstring& path);
		static inline void Close(SOCKET s) { ::closesocket(s); }
	};

	bool SendBuffer(SOCKET sock, const char* buffer, int len);
	bool ReceiveBuffer(SOCKET sock, char* buffer, int len);
}
