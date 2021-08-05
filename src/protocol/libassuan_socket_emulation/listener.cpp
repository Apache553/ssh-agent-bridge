
#include "../../log.h"
#include "../../util.h"
#include "listener.h"

#include <cassert>

#include <winsock2.h>
#include <sddl.h>
#include <WS2tcpip.h>
#include <bcrypt.h>

namespace sab
{
	class LibassuanSocketEmulationListenerData :public ListenerConnectionData
	{
	public:
		enum class State
		{
			Start,
			ReadNonce,
		};

		State state;
		OVERLAPPED overlapped;
		char nonce[LibassuanSocketEmulationListener::NONCE_LENGTH];
	};
}

sab::LibassuanSocketEmulationListener::LibassuanSocketEmulationListener(
	const std::wstring& socketPath,
	const std::wstring& listenAddress,
	std::shared_ptr<IConnectionManager> manager,
	bool permissionCheckFlag,
	bool writeWslMetadataFlag,
	const LxPermissionInfo& perm)
	:socketPath(socketPath),
	listenAddress(listenAddress),
	connectionManager(manager),
	permissionCheckFlag(permissionCheckFlag),
	writeWslMetadataFlag(writeWslMetadataFlag),
	perm(perm)
{
	LogInfo(L"set socket file path: ", socketPath);
	LogInfo(L"set listen address: ", listenAddress);
	memset(nonce, 0, NONCE_LENGTH);
	cancelEvent = CreateEventW(NULL, TRUE,
		FALSE, NULL);
	assert(cancelEvent != NULL);
}

bool sab::LibassuanSocketEmulationListener::Run()
{
	bool status = ListenLoop();
	if (status)
	{
		LogInfo(L"LibassuanSocketEmulationListener stopped gracefully.");
	}
	else
	{
		LogInfo(L"LibassuanSocketEmulationListener stopped unexpectedly.");
	}
	return status;
}

void sab::LibassuanSocketEmulationListener::Cancel()
{
	SetEvent(cancelEvent);
}

bool sab::LibassuanSocketEmulationListener::IsCancelled() const
{
	return WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0;
}

bool sab::LibassuanSocketEmulationListener::DoHandshake(
	std::shared_ptr<IoContext> context,
	int transferred)
{
	BOOL result;
	using DataType = LibassuanSocketEmulationListenerData;
	std::shared_ptr<DataType> data =
		std::static_pointer_cast<DataType>(context->listenerData);
	switch (data->state)
	{
	case DataType::State::Start:
		LogDebug(L"start authenticating.");
		data->state = DataType::State::ReadNonce;
		result = ReadFile(context->handle, data->nonce, NONCE_LENGTH,
			NULL, &data->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->Dispose();
		}
		return false;
	case DataType::State::ReadNonce:
		if (transferred != NONCE_LENGTH)
		{
			LogDebug(L"cannot read enough nonce");
			context->Dispose();
			return false;
		}
		if (memcmp(data->nonce, this->nonce, NONCE_LENGTH) != 0)
		{
			LogDebug(L"incorrect nonce!");
			context->Dispose();
			return false;
		}
		LogDebug(L"authentication succeed!");
		return true;
	default:
		LogDebug(L"illegal handshake status for assuan socket!");
		context->Dispose();
		return false;
	}
}

sab::LibassuanSocketEmulationListener::~LibassuanSocketEmulationListener()
{
	if (cancelEvent)
		CloseHandle(cancelEvent);
}

bool sab::LibassuanSocketEmulationListener::ListenLoop()
{
	SOCKET listenSocket;
	int result;
	HANDLE waitHandle[2];

	// prepare permission entries
	std::wostringstream sddlStream;
	PSECURITY_DESCRIPTOR sd;
	std::wstring sid = GetCurrentUserSidString();

	if (sid.empty())
	{
		return false;
	}

	// deny everyone except current user
	sddlStream << L"D:P(A;;GA;;;" << sid << ")(D;;GA;;;WD)";

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		sddlStream.str().c_str(),
		SDDL_REVISION_1,
		&sd,
		NULL))
	{
		LogError(L"cannot convert sddl to security descriptor!");
		return false;
	}
	auto sdGuard = HandleGuard(sd, LocalFree);

	// generate nonce
	if (BCryptGenRandom(NULL, reinterpret_cast<PUCHAR>(nonce), NONCE_LENGTH,
		BCRYPT_USE_SYSTEM_PREFERRED_RNG) != ERROR_SUCCESS)
	{
		LogError(L"cannot generate nonce!");
		return false;
	}

	// prepare event
	waitHandle[0] = cancelEvent;
	waitHandle[1] = WSACreateEvent();

	if (waitHandle[1] == NULL)
	{
		LogError(L"cannot create listen event object! ", LogLastError);
		return false;
	}
	auto heGuard = HandleGuard(waitHandle[1], WSACloseEvent);

	// prepare socket
	listenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSocket == INVALID_SOCKET)
	{
		LogError(L"cannot create socket! ", LogWSALastError);
		return false;
	}

	sockaddr_in socketAddress;
	int socketAddressLength = sizeof(socketAddress);
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = 0;
	auto listenAddress = WideStringToUtf8String(this->listenAddress);
	if (inet_pton(AF_INET, listenAddress.c_str(), &socketAddress.sin_addr) <= 0)
	{
		LogError(L"inet_pton failed!");
		return false;
	}

	if (bind(listenSocket, reinterpret_cast<sockaddr*>(&socketAddress),
		sizeof(socketAddress)) != 0)
	{
		LogError(L"bind failed!");
		return false;
	}

	// prepare socket file
	if (getsockname(listenSocket, reinterpret_cast<sockaddr*>(&socketAddress),
		&socketAddressLength) != 0)
	{
		LogError(L"cannot get binded port! ", LogWSALastError);
		return false;
	}

	HANDLE socketFileHandle = CreateFileW(socketPath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	auto socketFileGuard = HandleGuard(socketFileHandle, CloseHandle);
	if (socketFileHandle == INVALID_HANDLE_VALUE)
	{
		LogError(L"cannot create socket file! ", LogLastError);
		socketFileGuard.release();
		return false;
	}

	if (permissionCheckFlag) {
		if (SetFileSecurityW(socketPath.c_str(), DACL_SECURITY_INFORMATION, sd) == 0)
		{
			LogError(L"cannot set socket file permission! ", LogLastError);
			return false;
		}
	}

	if (writeWslMetadataFlag)
	{
		if (!SetLxPermissionByHandle(socketFileHandle, perm))
		{
			LogError(L"cannot set linux permission!");
			return false;
		}
	}

	int portNumber = ntohs(socketAddress.sin_port);
	{
		// write file content
		std::ostringstream socketFileStream(std::ios::out | std::ios::binary);
		socketFileStream << portNumber << '\n';
		socketFileStream.write(nonce, NONCE_LENGTH);
		std::string data = socketFileStream.str();
		DWORD writtenBytes = 0;
		BOOL result = WriteFile(socketFileHandle, data.data(),
			static_cast<DWORD>(data.length()), &writtenBytes, NULL);
		if (result == FALSE || writtenBytes != data.length())
		{
			LogError(L"cannot write socket file content!");
			return false;
		}
		FlushFileBuffers(socketFileHandle);
	}

	result = listen(listenSocket, 8);
	if (result != 0)
	{
		LogError(L"listen() failed! ", LogWSALastError);
		return false;
	}

	if (WSAEventSelect(listenSocket, waitHandle[1], FD_ACCEPT | FD_CLOSE) != 0)
	{
		LogError(L"WSAEventSelect failed! ", LogWSALastError);
		return false;
	}

	LogInfo(L"start listening on port ", portNumber, L" for ", socketPath);

	while (true)
	{
		int waitResult = WSAWaitForMultipleEvents(2, waitHandle, FALSE, WSA_INFINITE, FALSE);
		if (waitResult == WSA_WAIT_EVENT_0)
		{
			LogDebug(L"cancel requested! canceling...");
			return true;
		}
		else if (waitResult == WSA_WAIT_EVENT_0 + 1)
		{
			WSANETWORKEVENTS wne;
			if (WSAEnumNetworkEvents(listenSocket, waitHandle[1], &wne) != 0)
			{
				LogError(L"WSAEnumNetworkEvents failed! ", LogWSALastError);
				return false;
			}
			if (wne.lNetworkEvents & FD_CLOSE)
			{
				LogError(L"listening socket closed unexpectedly!");
				return false;
			}
			else if (wne.lNetworkEvents & FD_ACCEPT) {
				SOCKET acceptSocket = accept(listenSocket, NULL, NULL);
				if (acceptSocket == INVALID_SOCKET)
				{
					LogError(L"accept failed! ", LogWSALastError);
				}
				else
				{
					LogDebug(L"accepted a socket");
					auto data = std::make_shared<LibassuanSocketEmulationListenerData>();
					memset(data->nonce, 0, NONCE_LENGTH);
					memset(&data->overlapped, 0, sizeof(OVERLAPPED));
					data->state = LibassuanSocketEmulationListenerData::State::Start;
					if (!connectionManager->DelegateConnection(
						reinterpret_cast<HANDLE>(acceptSocket),
						this->shared_from_this(),
						data, true))
					{
						closesocket(acceptSocket);
					}
				}
			}
		}
		else
		{
			LogError(L"WSAWaitForMultipleEvents failed! ", LogWSALastError);
			return false;
		}
	}

}
