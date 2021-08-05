
#include "../../log.h"
#include "../../util.h"
#include "listener.h"

#include <cstring>
#include <thread>
#include <cassert>

#include <sddl.h>

sab::Win32NamedPipeListener::Win32NamedPipeListener(
	const std::wstring& pipePath,
	std::shared_ptr<IConnectionManager> manager,
	bool permissionCheckFlag)
	:pipePath(pipePath), connectionManager(manager),
	permissionCheckFlag(permissionCheckFlag)
{
	cancelEvent = CreateEventW(NULL, TRUE,
		FALSE, NULL);
	assert(cancelEvent != NULL);
}

bool sab::Win32NamedPipeListener::Run()
{
	bool status = ListenLoop();
	if (status)
	{
		LogInfo(L"Win32NamedPipeListener stopped gracefully.");
	}
	else
	{
		LogInfo(L"Win32NamedPipeListener stopped unexpectedly.");
	}
	return status;
}

bool sab::Win32NamedPipeListener::ListenLoop()
{
	HANDLE connectedEvent = NULL;
	HANDLE waitHandles[2];
	HANDLE iocpHandle = NULL;

	OVERLAPPED overlapped;
	SECURITY_ATTRIBUTES sa;
	std::wostringstream sddlStream;
	std::wstring sid = GetCurrentUserSidString();

	if (sid.empty())
	{
		return false;
	}

	// deny everyone except current user
	sddlStream << L"D:P(A;;GA;;;" << sid << ")(D;;GA;;;WD)";

	std::memset(&sa, 0, sizeof(SECURITY_ATTRIBUTES));
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = FALSE;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		sddlStream.str().c_str(),
		SDDL_REVISION_1,
		&sa.lpSecurityDescriptor,
		NULL))
	{
		LogError(L"cannot convert sddl to security descriptor!");
		return false;
	}
	auto sdGuard =
		HandleGuard(sa.lpSecurityDescriptor, LocalFree);

	overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (overlapped.hEvent == NULL)
	{
		LogError(L"cannot create event handle!");
		return false;
	}
	auto ceGuard = HandleGuard(overlapped.hEvent, CloseHandle);

	waitHandles[0] = cancelEvent;
	waitHandles[1] = overlapped.hEvent;

	iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
		NULL, 0, 0);
	if (iocpHandle == NULL)
	{
		LogError(L"cannot create io completion port!");
		return false;
	}
	auto iocpGuard = HandleGuard(iocpHandle, CloseHandle);

	LogInfo(L"start listening on ", pipePath);

	while (true)
	{

		HANDLE pipeHandle = CreateNamedPipeW(
			pipePath.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
			PIPE_UNLIMITED_INSTANCES,
			MAX_BUFFER_SIZE,
			MAX_BUFFER_SIZE,
			0,
			(permissionCheckFlag ? &sa : NULL));
		if (pipeHandle == INVALID_HANDLE_VALUE)
		{
			LogError(L"cannot create listener pipe! ", LogLastError);
			Cancel();
			return false;
		}
		auto pipeGuard = HandleGuard(pipeHandle, CloseHandle);

		if (ConnectNamedPipe(pipeHandle, &overlapped) != FALSE)
		{
			LogError(L"ConnectNamedPipe unexpectedly returned TRUE!");
			Cancel();
			return false;
		}

		switch (GetLastError())
		{
		case ERROR_IO_PENDING:
			LogDebug(L"there is no client connect yet. waiting...");
			break;
		case ERROR_PIPE_CONNECTED:
			LogDebug(L"client connects before ConnectNamedPipe is called!");
			SetEvent(connectedEvent);
			break;
		default:
			LogError(L"ConnectNamedPipe failed with error: ", LogLastError);
			Cancel();
			return false;
		}

		DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
		if (waitResult == WAIT_OBJECT_0)
		{
			// Cancel message received
			LogDebug(L"cancel requested! canceling...");
			return true;
		}
		else if (waitResult == WAIT_OBJECT_0 + 1)
		{
			// connected
			LogDebug(L"accepted new connection");
			if (connectionManager->DelegateConnection(pipeHandle,
				this->shared_from_this(), nullptr, false))
			{
				pipeGuard.release();
			}
		}
	}
}

void sab::Win32NamedPipeListener::Cancel()
{
	SetEvent(cancelEvent);
}

bool sab::Win32NamedPipeListener::IsCancelled() const
{
	return WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0;
}

bool sab::Win32NamedPipeListener::DoHandshake(std::shared_ptr<IoContext> context, int transferred)
{
	return true;
}

sab::Win32NamedPipeListener::~Win32NamedPipeListener()
{
	if (cancelEvent)
		CloseHandle(cancelEvent);
}
