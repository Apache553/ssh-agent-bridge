
#include "log.h"
#include "util.h"
#include "protocol_listener_unix_domain.h"

#include <cassert>

#include <winsock2.h>
#include <afunix.h>
#include <sddl.h>

static std::string WideStringToUtf8String(const std::wstring& str)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1,
		nullptr, 0, NULL, NULL);
	if (len == 0)
	{
		return std::string();
	}
	std::unique_ptr<char[]> buffer(new char[len]);
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1,
		buffer.get(), len, NULL, NULL);
	return std::string(buffer.get());
}

sab::UnixDomainSocketListener::UnixDomainSocketListener(
	const std::wstring& socketPath,
	std::shared_ptr<IocpListenerConnectionManager> manager)
	:socketPath(socketPath), connectionManager(manager)
{
	cancelEvent = CreateEventW(NULL, TRUE,
		FALSE, NULL);
	assert(cancelEvent != NULL);
}

bool sab::UnixDomainSocketListener::Run()
{
	bool status = ListenLoop();
	if (status)
	{
		LogInfo(L"UnixDomainSocketListener stopped gracefully.");
	}
	else
	{
		LogInfo(L"UnixDomainSocketListener stopped unexpectedly.");
	}
	return status;
}

void sab::UnixDomainSocketListener::Cancel()
{
	SetEvent(cancelEvent);
}

bool sab::UnixDomainSocketListener::IsCancelled() const
{
	return WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0;
}

bool sab::UnixDomainSocketListener::DoHandshake(std::shared_ptr<IoContext> context, int transferred)
{
	return true;
}

sab::UnixDomainSocketListener::~UnixDomainSocketListener()
{
	if (cancelEvent)
		CloseHandle(cancelEvent);
}

bool sab::UnixDomainSocketListener::ListenLoop()
{
	SOCKET listenSocket;
	int result;
	std::string u8SocketPath;

	HANDLE waitHandle[2];

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

	u8SocketPath = WideStringToUtf8String(socketPath);
	if (u8SocketPath.size() + 1 > UNIX_PATH_MAX)
	{
		LogError(L"socket path too long! must be less than ",
			UNIX_PATH_MAX, L" utf8 bytes!");
		return false;
	}

	waitHandle[0] = cancelEvent;
	waitHandle[1] = WSACreateEvent();

	if (waitHandle[1] == NULL)
	{
		LogError(L"cannot create listen event object! ", LogLastError);
		return false;
	}
	auto heGuard = HandleGuard(waitHandle[1], WSACloseEvent);

	listenSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listenSocket == INVALID_SOCKET)
	{
		LogError(L"cannot create socket! ", LogWSALastError);
		return false;
	}
	auto sockGuard = HandleGuard(reinterpret_cast<HANDLE>(listenSocket),
		[](HANDLE s) {closesocket(reinterpret_cast<SOCKET>(s)); });

	sockaddr_un socketAddress;
	memset(&socketAddress, 0, sizeof(sockaddr_un));
	socketAddress.sun_family = AF_UNIX;
	strncpy_s(socketAddress.sun_path, UNIX_PATH_MAX, u8SocketPath.c_str(), UNIX_PATH_MAX);

	result = bind(listenSocket, reinterpret_cast<sockaddr*>(&socketAddress),
		sizeof(socketAddress));
	if (result != 0)
	{
		LogError(L"cannot bind socket address! ", LogWSALastError);
		return false;
	}

	HANDLE socketFileHandle = CreateFileW(socketPath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING,
		FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT,
		NULL);
	auto socketFileGuard = HandleGuard(socketFileHandle, CloseHandle);
	if (socketFileHandle == INVALID_HANDLE_VALUE)
	{
		LogError(L"cannot set socket file close on exit! ", LogLastError);
		socketFileGuard.release();
	}

	if (SetFileSecurityW(socketPath.c_str(), DACL_SECURITY_INFORMATION, sd) == 0)
	{
		LogError(L"cannot set socket access permission! ", LogLastError);
		return false;
	}

	result = listen(listenSocket, 8);
	if (result != 0)
	{
		LogError(L"cannot listen address! ", LogWSALastError);
		return false;
	}

	if (WSAEventSelect(listenSocket, waitHandle[1], FD_ACCEPT | FD_CLOSE) != 0)
	{
		LogError(L"WSAEventSelect failed! ", LogWSALastError);
		return false;
	}

	LogInfo(L"start listening on ", socketPath);

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
					if (!connectionManager->DelegateConnection(
						reinterpret_cast<HANDLE>(acceptSocket),
						this->shared_from_this(),
						nullptr))
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
