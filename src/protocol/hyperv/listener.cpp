
#include "../../log.h"
#include "../../util.h"
#include "listener.h"

#include <cassert>

#include <WinSock2.h>
#include <hvsocket.h>
#include <combaseapi.h>

sab::HyperVSocketListener::HyperVSocketListener(
	const std::wstring& listenPort,
	std::shared_ptr<IConnectionManager> manager,
	const std::wstring& listenGUID,
	const std::wstring& listenServiceTemplate)
	:listenPort(listenPort),
	connectionManager(manager),
	listenGUID(listenGUID),
	listenServiceTemplate(listenServiceTemplate)
{
	cancelEvent = CreateEventW(NULL, TRUE,
		FALSE, NULL);
	assert(cancelEvent != NULL);
}

bool sab::HyperVSocketListener::Run()
{
	bool status = ListenLoop();
	if (status)
	{
		LogInfo(L"HyperVSocketListener stopped gracefully.");
	}
	else
	{
		LogInfo(L"HyperVSocketListener stopped unexpectedly.");
	}
	return status;
}

void sab::HyperVSocketListener::Cancel()
{
	SetEvent(cancelEvent);
}

bool sab::HyperVSocketListener::IsCancelled() const
{
	return WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0;
}

bool sab::HyperVSocketListener::DoHandshake(std::shared_ptr<IoContext> context, int transferred)
{
	return true;
}

sab::HyperVSocketListener::~HyperVSocketListener()
{
	if (cancelEvent)
		CloseHandle(cancelEvent);
}

bool sab::HyperVSocketListener::ListenLoop()
{
	SOCKET listenSocket;
	int result;
	int port = 0x44417A9F; // 1145141919

	GUID vmid = HV_GUID_ZERO;
	if (listenGUID.empty() || listenGUID == L"0" || listenGUID == L"wildcard")
	{
		vmid = HV_GUID_WILDCARD;
	}
	else if (listenGUID == L"children")
	{
		vmid = HV_GUID_CHILDREN;
	}
	else if (listenGUID == L"loopback")
	{
		vmid = HV_GUID_LOOPBACK;
	}
	else
	{
		HRESULT hr = IIDFromString(listenGUID.c_str(), &vmid);
		if (hr != S_OK)
		{
			LogError(L"invalid listen guid \"", listenGUID, L"\"");
			return false;
		}
	}
	LogInfo(L"using VmId ", LogGUID(vmid));

	GUID templateGuid = HV_GUID_VSOCK_TEMPLATE;
	if (!listenServiceTemplate.empty())
	{
		HRESULT hr = IIDFromString(listenServiceTemplate.c_str(), &templateGuid);
		if (hr != S_OK)
		{
			LogError(L"invalid template \"", listenServiceTemplate, L"\"");
			return false;
		}
	}


	if (!listenPort.empty()) {
		try {
			port = std::stoi(listenPort, nullptr, 0);
		}
		catch (...) {
			LogError(L"invalid port \"", listenPort, "\"");
			return false;
		}
	}
	LogInfo(L"using port ", std::hex, std::showbase, std::setfill(L'0'), std::setw(8), port);

	HANDLE waitHandle[2];

	waitHandle[0] = cancelEvent;
	waitHandle[1] = WSACreateEvent();

	if (waitHandle[1] == NULL)
	{
		LogError(L"cannot create listen event object! ", LogLastError);
		return false;
	}
	auto heGuard = HandleGuard(waitHandle[1], WSACloseEvent);

	listenSocket = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
	if (listenSocket == INVALID_SOCKET)
	{
		LogError(L"cannot create socket! ", LogWSALastError);
		return false;
	}
	auto sockGuard = HandleGuard(listenSocket, closesocket);

	SOCKADDR_HV socketAddress;
	memset(&socketAddress, 0, sizeof(socketAddress));
	socketAddress.Family = AF_HYPERV;
	socketAddress.VmId = vmid;
	socketAddress.ServiceId = templateGuid;
	socketAddress.ServiceId.Data1 = port;

	result = bind(listenSocket, reinterpret_cast<sockaddr*>(&socketAddress),
		sizeof(socketAddress));
	if (result != 0)
	{
		LogError(L"cannot bind socket address! ", LogWSALastError);
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

	LogInfo(L"start listening");

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

