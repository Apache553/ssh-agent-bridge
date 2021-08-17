
#include "../../log.h"
#include "../../util.h"
#include "listener.h"
#include "rebind_notifier.h"

#include <cassert>

#include <WinSock2.h>
#include <hvsocket.h>
#include <combaseapi.h>

SOCKET BuildHyperVListenSocket(GUID vmId, GUID serviceId);

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
	bool wsl2Flag = listenGUID == L"wsl2";
	bool socketGood = false;
	int port = 0x44417A9F; // 1145141919
	std::unique_ptr<WSL2SocketRebindNotifier> notifier;

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
	else if (listenGUID == L"wsl2")
	{
		try
		{
			notifier = std::make_unique<WSL2SocketRebindNotifier>();
			notifier->FlushLastWslVmId();
			if (!notifier->Start())
			{
				LogError(L"cannot start notifier!");
				return false;
			}
			vmid = notifier->GetLastWslVmId();
		}
		catch (...)
		{
			return false;
		}
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

	HANDLE waitHandle[3];
	DWORD waitHandleCount = 2;

	waitHandle[0] = cancelEvent;
	waitHandle[1] = WSACreateEvent();
	if (notifier)
	{
		waitHandle[2] = notifier->GetEventHandle();
		waitHandleCount += 1;
	}

	if (waitHandle[1] == NULL)
	{
		LogError(L"cannot create listen event object! ", LogLastError);
		return false;
	}
	auto heGuard = HandleGuard(waitHandle[1], WSACloseEvent);

	GUID serviceId = HV_GUID_VSOCK_TEMPLATE;
	serviceId.Data1 = port;

	wil::unique_socket listenSocket;
	if (vmid != GUID_NULL || !wsl2Flag) {
		listenSocket.reset(BuildHyperVListenSocket(vmid, serviceId));
	}
	else
	{
		LogInfo(L"cannot get wsl2 vmid, wsl2 vm may be not running!");
	}

	if (!listenSocket.is_valid())
	{
		if (wsl2Flag)
		{
			LogInfo(L"wsl2 hyperv socket is temporarily unavailable!");
			socketGood = false;
		}
		else
		{
			return false;
		}
	}
	else
	{
		socketGood = true;
		if (WSAEventSelect(listenSocket.get(), waitHandle[1], FD_ACCEPT | FD_CLOSE) != 0)
		{
			LogError(L"WSAEventSelect failed! ", LogWSALastError);
			return false;
		}
	}



	LogInfo(L"start processing");

	while (true)
	{
		int waitResult = WSAWaitForMultipleEvents(waitHandleCount, waitHandle, FALSE, WSA_INFINITE, FALSE);
		if (waitResult == WSA_WAIT_EVENT_0)
		{
			LogDebug(L"cancel requested! canceling...");
			return true;
		}
		else if (waitResult == WSA_WAIT_EVENT_0 + 1)
		{
			WSANETWORKEVENTS wne;
			if (WSAEnumNetworkEvents(listenSocket.get(), waitHandle[1], &wne) != 0)
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
				SOCKET acceptSocket = accept(listenSocket.get(), NULL, NULL);
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
		else if (waitResult == WSA_WAIT_EVENT_0 + 2)
		{
			GUID newVmid = notifier->GetLastWslVmId();
			ResetEvent(waitHandle[2]);
			if (vmid != newVmid || !socketGood)
			{
				vmid = newVmid;
				listenSocket.reset(BuildHyperVListenSocket(vmid, serviceId));
				if (listenSocket.is_valid())
				{
					LogInfo(L"rebind socket to ", LogGUID(vmid));
					if (WSAEventSelect(listenSocket.get(), waitHandle[1], FD_ACCEPT | FD_CLOSE) != 0)
					{
						LogError(L"WSAEventSelect failed! ", LogWSALastError);
						return false;
					}
					socketGood = true;
				}
				else
				{
					LogInfo(L"socket is temporarily unavailable!");
					socketGood = false;
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

SOCKET BuildHyperVListenSocket(GUID vmId, GUID serviceId)
{
	SOCKET listenSocket;
	int result;
	listenSocket = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
	if (listenSocket == INVALID_SOCKET)
	{
		LogError(L"cannot create socket! ", LogWSALastError);
		return INVALID_SOCKET;
	}
	auto sockGuard = sab::HandleGuard(listenSocket, closesocket);

	SOCKADDR_HV socketAddress;
	memset(&socketAddress, 0, sizeof(socketAddress));
	socketAddress.Family = AF_HYPERV;
	socketAddress.VmId = vmId;
	socketAddress.ServiceId = serviceId;

	result = bind(listenSocket, reinterpret_cast<sockaddr*>(&socketAddress),
		sizeof(socketAddress));
	if (result != 0)
	{
		LogError(L"cannot bind socket address! ", LogWSALastError);
		return INVALID_SOCKET;
	}

	result = listen(listenSocket, 8);
	if (result != 0)
	{
		LogError(L"cannot listen socket! ", LogWSALastError);
		return INVALID_SOCKET;
	}

	sockGuard.release();
	return listenSocket;
}
