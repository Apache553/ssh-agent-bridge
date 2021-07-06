
#include "log.h"
#include "util.h"
#include "protocol_listener_win32_namedpipe.h"

#include <cstring>
#include <thread>
#include <cassert>

#include <sddl.h>

#pragma comment(lib, "Ws2_32.lib")

sab::Win32NamedPipeListener::Win32NamedPipeListener(
	const std::wstring& pipePath
)
	:pipePath(pipePath)
{
	cancelEvent = CreateEventW(NULL, TRUE,
		FALSE, NULL);
}

void sab::Win32NamedPipeListener::Run()
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
}

bool sab::Win32NamedPipeListener::ListenLoop()
{
	HANDLE connectedEvent = NULL;
	HANDLE waitHandles[2];
	HANDLE iocpHandle = NULL;

	OVERLAPPED overlapped;
	SECURITY_ATTRIBUTES sa;
	const wchar_t pipeSDDL[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x12019b;;;AU)";

	std::memset(&sa, 0, sizeof(SECURITY_ATTRIBUTES));
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = FALSE;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		pipeSDDL,
		SDDL_REVISION_1,
		&sa.lpSecurityDescriptor,
		&sa.nLength))
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

	// io completion thread
	std::thread iocpThread([&]()
		{
			IocpThreadProc(iocpHandle);
		});
	iocpThread.detach();

	while (true)
	{
		auto context = std::make_shared<PipeContext>();

		context->pipeHandle = CreateNamedPipeW(
			pipePath.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
			PIPE_UNLIMITED_INSTANCES,
			MAX_PIPE_BUFFER_SIZE,
			MAX_PIPE_BUFFER_SIZE,
			0,
			nullptr);
		if (context->pipeHandle == INVALID_HANDLE_VALUE)
		{
			LogError(L"cannot create listener pipe! ", LogLastError);
			Cancel();
			return false;
		}

		if (ConnectNamedPipe(context->pipeHandle, &overlapped) != FALSE)
		{
			LogError(L"ConnectNamedPipe unexpectedly returned TRUE!");
			Cancel();
			return false;
		}

		switch (GetLastError())
		{
		case ERROR_IO_PENDING:
			LogDebug(L"waiting for connection...");
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

		context->pipeStatus = PipeContext::Status::Listening;

		DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
		if (waitResult == WAIT_OBJECT_0)
		{
			// Cancel message received
			return true;
		}
		else if (waitResult == WAIT_OBJECT_0 + 1)
		{
			// connected
			LogDebug(L"accepted new connection");
			
			if (CreateIoCompletionPort(context->pipeHandle, iocpHandle,
				reinterpret_cast<ULONG_PTR>(context.get()), 0) != iocpHandle)
			{
				LogDebug(L"cannot assign pipe to iocp");
				continue;
			}

			std::list<std::shared_ptr<PipeContext>>::iterator iter;
			{
				std::lock_guard<std::mutex> lg(listMutex);
				contextList.emplace_front(context);
				iter = contextList.begin();
			}
			(*iter)->selfIterator = iter;
			(*iter)->message.source = this->shared_from_this();
			
			OnIoCompletion(iter->get(), true);
		}
	}
}

void sab::Win32NamedPipeListener::OnIoCompletion(PipeContext* context, bool noRealIo)
{
	BOOL result;
	DWORD transferred = 0;
	uint32_t beLength;
	DWORD writeLength;
	DWORD readLength;

	if (!noRealIo && (GetOverlappedResult(context->pipeHandle, &context->overlapped,
		&transferred, FALSE) == FALSE))
	{
		LogDebug(L"failed to get overlapped operation result!");
		FinalizeContext(context);
		return;
	}
	LogDebug(L"OnIoCompletion: ", static_cast<int>(context->pipeStatus));
	switch (context->pipeStatus)
	{
	case PipeContext::Status::Listening:
		context->pipeStatus = PipeContext::Status::ReadHeader;
		context->needTransferBytes = HEADER_SIZE;

		result = ReadFile(context->pipeHandle, context->buffer,
			HEADER_SIZE, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			FinalizeContext(context);
			return;
		}
		break;
	case PipeContext::Status::ReadHeader:
		context->pipeStatus = PipeContext::Status::ReadBody;
		context->needTransferBytes -= transferred;

		// Check transferred length
		if (context->needTransferBytes != 0)
		{
			FinalizeContext(context);
			return;
		}

		// tweak byte order
		memcpy(&beLength, context->buffer, HEADER_SIZE);
		context->message.length = ntohl(beLength);

		if (context->message.length > MAX_MESSAGE_SIZE)
		{
			LogDebug(L"message too long: ", context->message.length);
			FinalizeContext(context);
			return;
		}

		context->needTransferBytes = context->message.length;

		// Compute read length
		readLength = min(MAX_PIPE_BUFFER_SIZE, context->needTransferBytes);

		result = ReadFile(context->pipeHandle, context->buffer,
			readLength, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			FinalizeContext(context);
			return;
		}
		break;
	case PipeContext::Status::ReadBody:
		context->needTransferBytes -= transferred;
		if (context->needTransferBytes < 0)
		{
			LogDebug(L"unexpectedly read too many data");
			FinalizeContext(context);
			return;
		}

		// append read data
		context->message.data.insert(context->message.data.end(), context->buffer,
			context->buffer + transferred);

		if (context->needTransferBytes > 0)
		{
			// continue reading...
			readLength = min(MAX_PIPE_BUFFER_SIZE, context->needTransferBytes);

			result = ReadFile(context->pipeHandle, context->buffer,
				readLength, NULL, &context->overlapped);
			if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				FinalizeContext(context);
				return;
			}
			break;
		}
		else
		{
			// finished read
			// TODO: handle message enqueue here
			context->externalReference = true;
			LogDebug(L"read finished.");
			context->externalReference = false;
			FinalizeContext(context);
		}
		break;
	case PipeContext::Status::WaitReply:
		context->pipeStatus = PipeContext::Status::Write;
		// reply filled into message member
		beLength = htonl(context->message.length);
		context->transferredBytes = -static_cast<int>(HEADER_SIZE);
		context->needTransferBytes = static_cast<int>(context->message.data.size()) + HEADER_SIZE;

		memcpy(context->buffer, &beLength, HEADER_SIZE);

		writeLength = min(MAX_PIPE_BUFFER_SIZE, context->needTransferBytes);

		memcpy(context->buffer + HEADER_SIZE, context->message.data.data(),
			writeLength - HEADER_SIZE);

		result = WriteFile(context->pipeHandle, context->buffer,
			writeLength, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			FinalizeContext(context);
			return;
		}
		break;
	case PipeContext::Status::Write:
		context->needTransferBytes -= transferred;
		context->transferredBytes += transferred;
		if (context->needTransferBytes > 0)
		{
			// continue write
			writeLength = min(MAX_PIPE_BUFFER_SIZE, context->needTransferBytes);
			memcpy(context->buffer,
				context->message.data.data() + context->transferredBytes,
				writeLength);
			result = WriteFile(context->pipeHandle, context->buffer,
				writeLength, NULL, &context->overlapped);
			if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				FinalizeContext(context);
				return;
			}
		}
		else
		{
			// finished writing
			// prepare for next message
			context->pipeStatus = PipeContext::Status::Listening;
			OnIoCompletion(context, true);
		}
		break;
	default:
		LogDebug("illegal status for pipe context!");
		FinalizeContext(context);
		return;
	}
}

void sab::Win32NamedPipeListener::FinalizeContext(PipeContext* context)
{
	// make sure it can be deleted from list once
	if (!context->disposed && !context->externalReference)
	{
		CloseHandle(context->pipeHandle);
		context->pipeHandle = INVALID_HANDLE_VALUE;
		// after this the context may be not valid anymore
		{
			std::lock_guard<std::mutex> lg(listMutex);
			contextList.erase(context->selfIterator);
		}
	}
	context->pipeStatus = PipeContext::Status::Destroyed;
}

void sab::Win32NamedPipeListener::IocpThreadProc(HANDLE iocpHandle)
{
	OVERLAPPED* overlapped;
	DWORD bytes;
	PipeContext* context;

	while (!IsCancelled())
	{
		overlapped = nullptr;
		context = nullptr;
		if (GetQueuedCompletionStatus(iocpHandle, &bytes,
			reinterpret_cast<PULONG_PTR>(&context), &overlapped, INFINITE) == FALSE)
		{
			// error
			LogDebug(L"GetQueuedCompletionStatus failed! ", LogLastError);
			if (context)
			{
				FinalizeContext(context);
			}
		}
		else
		{
			OnIoCompletion(context);
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

void sab::Win32NamedPipeListener::PostBackReply(SshMessageEnvelope* message)
{
	PipeContext* context = reinterpret_cast<PipeContext*>(message->id);
	context->externalReference = false;
	OnIoCompletion(context, true);
}

sab::Win32NamedPipeListener::~Win32NamedPipeListener()
{
	if (cancelEvent)
		CloseHandle(cancelEvent);
}

sab::Win32NamedPipeListener::PipeContext::PipeContext()
{
	memset(&overlapped, 0, sizeof(OVERLAPPED));
	pipeHandle = INVALID_HANDLE_VALUE;
	pipeStatus = Status::Initialized;
	externalReference = false;
	disposed = false;
	message.id = reinterpret_cast<void*>(this);
}

sab::Win32NamedPipeListener::PipeContext::~PipeContext()
{
	if (pipeHandle != INVALID_HANDLE_VALUE)
		CloseHandle(pipeHandle);
}
