
#include "log.h"
#include "util.h"
#include "protocol_listener_iocp_connection_manager.h"

#include <cassert>

#include <WinSock2.h>

static constexpr const wchar_t* IoContextStateToString(sab::IoContext::State state)
{
	using State = sab::IoContext::State;
	switch (state)
	{
	case State::Initialized:
		return L"Initialized";
	case State::Destroyed:
		return L"Destroyed";
	case State::Handshake:
		return L"Handshake";
	case State::ReadBody:
		return L"ReadBody";
	case State::ReadHeader:
		return L"ReadHeader";
	case State::WriteReply:
		return L"WriteReply";
	case State::WaitReply:
		return L"WaitReply";
	case State::Ready:
		return L"Ready";
	default:
		return L"Unknown";
	}
}

sab::IoContext::IoContext()
	:handle(INVALID_HANDLE_VALUE), state(State::Initialized),
	ioDataOffest(0), ioBufferOffest(0), ioNeedBytes(0),
	holdFlag(false)
{
	memset(&overlapped, 0, sizeof(OVERLAPPED));
}

sab::IoContext::~IoContext()
{
	if (handle != INVALID_HANDLE_VALUE)
		CloseHandle(handle);
}

void sab::IoContext::Dispose()
{
	if (state != State::Destroyed) {
		LogDebug(L"terminating connection: ", handle);
		if (isSocket) {
			closesocket(reinterpret_cast<SOCKET>(handle));
		}
		else
		{
			CloseHandle(handle);
		}
		handle = INVALID_HANDLE_VALUE;
	}
	state = State::Destroyed;
	if (!holdFlag)
	{
		owner->RemoveContext(this);
	}
}

void sab::IoContext::PrepareIo()
{
	++holdFlag;
}

void sab::IoContext::DoneIo()
{
	--holdFlag;
}

sab::IocpListenerConnectionManager::IocpListenerConnectionManager()
	:cancelEvent(NULL), iocpHandle(NULL), initialized(false)
{
}

sab::IocpListenerConnectionManager::~IocpListenerConnectionManager()
{
	if (cancelEvent != NULL) {
		Stop();
		CloseHandle(cancelEvent);
	}
}

bool sab::IocpListenerConnectionManager::Initialize()
{
	cancelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (cancelEvent == NULL)
		return false;

	initialized = true;
	return true;
}

bool sab::IocpListenerConnectionManager::Start()
{
	if (!initialized)return false;
	ResetEvent(cancelEvent);
	iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (iocpHandle == NULL) {
		LogError(L"cannot create completion port!");
		return false;
	}
	try
	{
		iocpThread = std::thread([this]()
			{
				IocpThreadProc();
			});
	}
	catch (...)
	{
		return false;
	}
	return true;
}

void sab::IocpListenerConnectionManager::Stop()
{
	if (!initialized)return;
	SetEvent(cancelEvent);
	if (iocpHandle) {
		CloseHandle(iocpHandle);
		iocpHandle = NULL;
	}
	if (iocpThread.joinable())
		iocpThread.join();
}

bool sab::IocpListenerConnectionManager::DelegateConnection(
	HANDLE connection,
	std::shared_ptr<ProtocolListenerBase> listener,
	std::shared_ptr<ListenerConnectionData> data,
	bool isSocket)
{
	auto context = std::make_shared<IoContext>();

	if (!CreateIoCompletionPort(connection, iocpHandle,
		reinterpret_cast<ULONG_PTR>(context.get()), 0))
	{
		LogDebug(L"cannot associate handle to completion port! ", LogLastError);
		return false;
	}
	LogDebug(L"delegated connection to manager: ", connection);
	context->handle = connection;
	context->listener = listener;
	context->listenerData = data;
	context->isSocket = isSocket;
	context->state = IoContext::State::Handshake;

	context->owner = shared_from_this();

	std::weak_ptr<IoContext> weakContext(context);
	context->message.replyCallback = [this, weakContext](SshMessageEnvelope* message, bool status)
	{
		auto strongContext = weakContext.lock();
		if (strongContext == nullptr)
			return; // context destroyed
		PostMessageReply(strongContext, message, status);
	};

	{
		std::lock_guard<std::mutex> lg(listMutex);
		contextList.emplace_front(context);
		context->selfIter = contextList.begin();
	}
	DoIoCompletion(context, true);
	return true;
}

void sab::IocpListenerConnectionManager::SetEmitMessageCallback(std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)>&& callback)
{
	receiveCallback = callback;
}

void sab::IocpListenerConnectionManager::RemoveContext(IoContext* context)
{
	std::lock_guard<std::mutex> lg(listMutex);
	contextList.erase(context->selfIter);
}

void sab::IocpListenerConnectionManager::IocpThreadProc()
{
	OVERLAPPED* overlapped;
	DWORD bytes;
	IoContext* context;

	LogDebug(L"started iocp thread");
	
	while (WaitForSingleObject(cancelEvent, 0) != WAIT_OBJECT_0)
	{
		overlapped = nullptr;
		context = nullptr;
		if (GetQueuedCompletionStatus(iocpHandle, &bytes,
			reinterpret_cast<PULONG_PTR>(&context), &overlapped, INFINITE) == FALSE)
		{
			// error
			if (GetLastError() == ERROR_BROKEN_PIPE) {
				LogDebug(L"remote closed pipe.");
			}
			else if (GetLastError() == ERROR_ABANDONED_WAIT_0)
			{
				LogDebug(L"completion port closed.");
				break;
			}
			else {
				LogDebug(L"GetQueuedCompletionStatus failed! ", LogLastError);
			}
			if (context)
			{
				context->DoneIo();
				context->Dispose();
			}
		}
		else
		{
			context->DoneIo();
			DoIoCompletion(context->shared_from_this(), false);
		}
	}
}

void sab::IocpListenerConnectionManager::DoIoCompletion(std::shared_ptr<IoContext> context, bool noRealIo)
{
	BOOL result;
	DWORD transferred = 0;
	uint32_t beLength;
	int writeLength;
	int readLength;
	IIocpListener* iocpListener;

	if (!noRealIo && (GetOverlappedResult(context->handle, &context->overlapped,
		&transferred, FALSE) == FALSE))
	{
		LogDebug(L"failed to get overlapped operation result!");
		context->Dispose();
		return;
	}
	LogDebug(L"current state: ", IoContextStateToString(context->state));
	switch (context->state)
	{
	case IoContext::State::Handshake:
		iocpListener = dynamic_cast<IIocpListener*>(context->listener.get());
		assert(iocpListener != nullptr);
		if (!iocpListener->DoHandshake(context, transferred))return;
	case IoContext::State::Ready:
		context->state = IoContext::State::ReadHeader;
		context->ioNeedBytes = HEADER_SIZE;
		context->message.data.clear();

		context->PrepareIo();
		result = ReadFile(context->handle, context->ioBuffer,
			HEADER_SIZE, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->DoneIo();
			context->Dispose();
			return;
		}
		break;
	case IoContext::State::ReadHeader:
		context->state = IoContext::State::ReadBody;
		context->ioNeedBytes -= transferred;

		// Check transferred length
		if (context->ioNeedBytes != 0)
		{
			context->Dispose();
			return;
		}

		// tweak byte order
		memcpy(&beLength, context->ioBuffer, HEADER_SIZE);
		context->message.length = ntohl(beLength);

		if (context->message.length > MAX_MESSAGE_SIZE)
		{
			LogDebug(L"message too long: ", context->message.length);
			context->Dispose();
			return;
		}

		context->ioNeedBytes = context->message.length;

		// Compute read length
		readLength = min(MAX_BUFFER_SIZE, context->ioNeedBytes);

		context->PrepareIo();
		result = ReadFile(context->handle, context->ioBuffer,
			readLength, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->DoneIo();
			context->Dispose();
			return;
		}
		break;
	case IoContext::State::ReadBody:
		context->ioNeedBytes -= transferred;
		if (context->ioNeedBytes < 0)
		{
			LogDebug(L"unexpectedly read too many data");
			context->Dispose();
			return;
		}

		// append read data
		context->message.data.insert(context->message.data.end(), context->ioBuffer,
			context->ioBuffer + transferred);

		if (context->ioNeedBytes > 0)
		{
			// continue reading...
			readLength = min(MAX_BUFFER_SIZE, context->ioNeedBytes);

			context->PrepareIo();
			result = ReadFile(context->handle, context->ioBuffer,
				readLength, NULL, &context->overlapped);
			if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				context->DoneIo();
				context->Dispose();
				return;
			}
		}
		else
		{
			// finished read
			context->state = IoContext::State::WaitReply;
			LogDebug(L"recv message: length=", context->message.length, L", type=0x",
				std::hex, std::setfill(L'0'), std::setw(2), context->message.data[0]);
			receiveCallback(&context->message, context);
		}
		break;
	case IoContext::State::WaitReply:
		context->state = IoContext::State::WriteReply;
		LogDebug(L"send message: length=", context->message.length, L", type=0x",
			std::hex, std::setfill(L'0'), std::setw(2), context->message.data[0]);
		// reply already filled into message member
		beLength = htonl(context->message.length);
		context->ioBufferOffest = static_cast<int>(HEADER_SIZE);
		context->ioNeedBytes = static_cast<int>(context->message.data.size()) + HEADER_SIZE;

		memcpy(context->ioBuffer, &beLength, HEADER_SIZE);

		writeLength = min(MAX_BUFFER_SIZE, context->ioNeedBytes);

		context->ioDataOffest = writeLength - HEADER_SIZE;
		memcpy(context->ioBuffer + HEADER_SIZE, context->message.data.data(),
			context->ioDataOffest);

		context->PrepareIo();
		result = WriteFile(context->handle, context->ioBuffer,
			writeLength, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->DoneIo();
			context->Dispose();
			return;
		}
		break;
	case IoContext::State::WriteReply:
		context->ioNeedBytes -= transferred;
		if (context->ioNeedBytes > 0)
		{
			// continue write
			writeLength = min(MAX_BUFFER_SIZE, context->ioNeedBytes);
			memcpy(context->ioBuffer,
				context->message.data.data() + context->ioDataOffest,
				writeLength);
			context->ioDataOffest += writeLength;

			context->PrepareIo();
			result = WriteFile(context->handle, context->ioBuffer,
				writeLength, NULL, &context->overlapped);
			if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				context->DoneIo();
				context->Dispose();
				return;
			}
		}
		else
		{
			// finished writing
			// prepare for next message
			context->state = IoContext::State::Ready;
			DoIoCompletion(context, true);
		}
		break;
	default:
		LogDebug(L"illegal status for pipe context!");
		context->Dispose();
		return;
	}
}

void sab::IocpListenerConnectionManager::PostMessageReply(std::shared_ptr<void> genericContext, SshMessageEnvelope* message, bool status)
{
	std::shared_ptr<IoContext> context = std::static_pointer_cast<IoContext>(genericContext);
	if (status) {
		DoIoCompletion(context, true);
	}
	else
	{
		context->Dispose();
	}
}


