
#include "../../log.h"
#include "../../util.h"
#include "proxy.h"
#include "../libassuan_socket_emulation/connector.h"
#include "../namedpipe/connector.h"

#include <cassert>

#include <WinSock2.h>

SOCKET DuplicateSocket(SOCKET s);

static constexpr const wchar_t* IoContextStateToString(sab::ProxyIoContext::State state)
{
	using State = sab::ProxyIoContext::State;
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

sab::ProxyIoContext::ProxyIoContext()
	:state(State::Initialized),
	ioDataOffset(0), ioBufferOffset(0)
{
	memset(&overlapped, 0, sizeof(overlapped));
}

sab::ProxyIoContext::~ProxyIoContext()
{
	if (handle != INVALID_HANDLE_VALUE) {
		CloseIoHandle(handle, handleType);
		LogDebug(L"closed handle ", handle);
	}
}

void sab::ProxyIoContext::Dispose()
{
	// potential data race here
	if (state != State::Destroyed) {
		state = State::Destroyed;
		LogDebug(L"terminating connection: ", handle);
		// any pending io operation will be cancelled after close
		// we can safely clean the context out of the list
		CancelIoEx(handle, nullptr);
		owner->RemoveContext(this);
	}
}

sab::ProxyConnectionManager::ProxyConnectionManager()
	:cancelFlag(false), iocpHandle(NULL), initialized(false)
{
}

sab::ProxyConnectionManager::~ProxyConnectionManager()
{
	if (!cancelFlag) {
		Stop();
	}
}

bool sab::ProxyConnectionManager::Initialize()
{
	initialized = true;
	return true;
}

bool sab::ProxyConnectionManager::Start()
{
	if (!initialized)return false;
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

void sab::ProxyConnectionManager::Stop()
{
	if (!initialized)return;
	cancelFlag = true;
	if (iocpHandle) {
		CloseHandle(iocpHandle);
		iocpHandle = NULL;
	}
	if (iocpThread.joinable())
		iocpThread.join();
}

bool sab::ProxyConnectionManager::DelegateConnection(
	HANDLE connection,
	std::shared_ptr<ProtocolListenerBase> listener,
	std::shared_ptr<ListenerConnectionData> data,
	bool isSocket)
{
	auto context = std::make_shared<ProxyIoContext>();

	if (!CreateIoCompletionPort(connection, iocpHandle,
		reinterpret_cast<ULONG_PTR>(std::static_pointer_cast<IoContext>(context).get()), 0))
	{
		LogDebug(L"cannot associate handle to completion port! ", LogLastError);
		return false;
	}
	LogDebug(L"delegated connection to manager: ", connection);
	context->handle = connection;
	context->listener = listener;
	context->listenerData = data;
	context->handleType = isSocket ? IoContext::HandleType::SocketHandle : IoContext::HandleType::FileHandle;
	context->state = ProxyIoContext::State::Handshake;

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
	DoIoCompletion(context, 0);
	return true;
}

void sab::ProxyConnectionManager::SetEmitMessageCallback(std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)>&& callback)
{
	receiveCallback = callback;
}

void sab::ProxyConnectionManager::RemoveContext(IoContext* context)
{
	std::lock_guard<std::mutex> lg(listMutex);
	contextList.erase(context->selfIter);
}

void sab::ProxyConnectionManager::IocpThreadProc()
{
	OVERLAPPED* overlapped;
	DWORD bytes;
	IoContext* context;

	LogDebug(L"started iocp thread");

	while (!cancelFlag)
	{
		overlapped = nullptr;
		context = nullptr;
		bytes = 0;
		if (GetQueuedCompletionStatus(iocpHandle, &bytes,
			reinterpret_cast<PULONG_PTR>(&context), &overlapped, INFINITE) == FALSE)
		{
			// error
			if (GetLastError() == ERROR_OPERATION_ABORTED)
			{
				// cancelled operation
			}
			else if (GetLastError() == ERROR_BROKEN_PIPE) {
				LogDebug(L"remote unexpectedly closed pipe.");
				// dispose context
				if (context)
					context->Dispose();
			}
			else if (GetLastError() == ERROR_ABANDONED_WAIT_0)
			{
				LogDebug(L"completion port closed.");
				break;
			}
			else {
				LogDebug(L"GetQueuedCompletionStatus failed! ", LogLastError);
				if (context)
					context->Dispose();
			}
		}
		else
		{
			DoIoCompletion(std::static_pointer_cast<ProxyIoContext>(context->shared_from_this()), bytes);
		}
	}
}

void sab::ProxyConnectionManager::DoIoCompletion(std::shared_ptr<ProxyIoContext> context, DWORD transferred)
{
	BOOL result;
	uint32_t beLength;
	int writeLength;
	int readLength;
	IManagedListener* managedListener;

	LogDebug(L"current state: ", IoContextStateToString(context->state));
	switch (context->state)
	{
	case ProxyIoContext::State::Handshake:
		managedListener = dynamic_cast<IManagedListener*>(context->listener.get());
		assert(managedListener != nullptr);
		if (!managedListener->DoHandshake(context, transferred))return;
	case ProxyIoContext::State::Ready:
		context->state = ProxyIoContext::State::ReadHeader;
		context->ioNeedBytes = HEADER_SIZE;
		context->message.data.clear();

		result = ReadFile(context->handle, context->ioBuffer,
			HEADER_SIZE, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->Dispose();
			return;
		}
		break;
	case ProxyIoContext::State::ReadHeader:
		context->state = ProxyIoContext::State::ReadBody;
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

		result = ReadFile(context->handle, context->ioBuffer,
			readLength, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->Dispose();
			return;
		}
		break;
	case ProxyIoContext::State::ReadBody:
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

			result = ReadFile(context->handle, context->ioBuffer,
				readLength, NULL, &context->overlapped);
			if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				context->Dispose();
				return;
			}
		}
		else
		{
			// finished read
			context->state = ProxyIoContext::State::WaitReply;
			LogDebug(L"recv message: length=", context->message.length, L", type=0x",
				std::hex, std::setfill(L'0'), std::setw(2), context->message.data[0]);
			receiveCallback(&context->message, context);
		}
		break;
	case ProxyIoContext::State::WaitReply:
		context->state = ProxyIoContext::State::WriteReply;
		LogDebug(L"send message: length=", context->message.length, L", type=0x",
			std::hex, std::setfill(L'0'), std::setw(2), context->message.data[0]);
		// reply already filled into message member
		beLength = htonl(context->message.length);
		context->ioBufferOffset = static_cast<int>(HEADER_SIZE);
		context->ioNeedBytes = static_cast<int>(context->message.data.size()) + HEADER_SIZE;

		memcpy(context->ioBuffer, &beLength, HEADER_SIZE);

		writeLength = min(MAX_BUFFER_SIZE, context->ioNeedBytes);

		context->ioDataOffset = writeLength - HEADER_SIZE;
		memcpy(context->ioBuffer + HEADER_SIZE, context->message.data.data(),
			context->ioDataOffset);

		result = WriteFile(context->handle, context->ioBuffer,
			writeLength, NULL, &context->overlapped);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->Dispose();
			return;
		}
		break;
	case ProxyIoContext::State::WriteReply:
		context->ioNeedBytes -= transferred;
		if (context->ioNeedBytes > 0)
		{
			// continue write
			writeLength = min(MAX_BUFFER_SIZE, context->ioNeedBytes);
			memcpy(context->ioBuffer,
				context->message.data.data() + context->ioDataOffset,
				writeLength);
			context->ioDataOffset += writeLength;

			result = WriteFile(context->handle, context->ioBuffer,
				writeLength, NULL, &context->overlapped);
			if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				context->Dispose();
				return;
			}
		}
		else
		{
			// finished writing
			// prepare for next message
			context->state = ProxyIoContext::State::Ready;
			DoIoCompletion(context, 0);
		}
		break;
	default:
		LogDebug(L"illegal status for pipe context!");
		context->Dispose();
		return;
	}
}

void sab::ProxyConnectionManager::PostMessageReply(std::shared_ptr<void> genericContext, SshMessageEnvelope* message, bool status)
{
	auto context = std::static_pointer_cast<ProxyIoContext>(genericContext);
	if (status) {
		DoIoCompletion(context, 0);
	}
	else
	{
		context->Dispose();
	}
}
