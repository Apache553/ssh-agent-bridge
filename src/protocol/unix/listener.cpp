
#include "../../log.h"
#include "../../util.h"
#include "../../lxperm.h"
#include "listener.h"

#include <cassert>

#include <winsock2.h>
#include <afunix.h>
#include <sddl.h>

sab::UnixDomainSocketListener::UnixDomainSocketListener(
	const std::wstring& socketPath,
	std::shared_ptr<IConnectionManager> manager,
	bool permissionCheckFlag,
	bool writeWslMetadataFlag,
	const LxPermissionInfo& perm)
	:socketPath(socketPath), connectionManager(manager),
	permissionCheckFlag(permissionCheckFlag),
	writeWslMetadataFlag(writeWslMetadataFlag),
	perm(perm)
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

	// always delete the socket file first
	if (CheckFileExists(socketPath))
	{
		LogWarning(L"socket exists! deleting...");
		if (DeleteFileW(socketPath.c_str()) == 0)
		{
			LogWarning(L"cannot delete the file! ", LogLastError);
		}
	}

	listenSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listenSocket == INVALID_SOCKET)
	{
		LogError(L"cannot create socket! ", LogWSALastError);
		return false;
	}
	auto sockGuard = HandleGuard(listenSocket, closesocket);

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

	// automatic delete the socket file on exit
	auto fileGuard = HandleGuard(socketPath, [](const std::wstring& path)
		{
			DeleteFileW(path.c_str());
		});

	if (permissionCheckFlag) {
		if (SetFileSecurityW(socketPath.c_str(), DACL_SECURITY_INFORMATION, sd) == 0)
		{
			LogError(L"cannot set socket access permission! ", LogLastError);
			return false;
		}
	}

	if (writeWslMetadataFlag) {
		if (!SetLxPermission(GetPathParentDirectory(socketPath), perm, false))
		{
			LogError(L"cannot set linux permission!");
			return false;
		}
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
						nullptr, true))
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
