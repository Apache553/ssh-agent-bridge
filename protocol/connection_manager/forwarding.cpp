

#include "../../log.h"
#include "../../util.h"
#include "forwarding.h"
#include "../libassuan_socket_emulation/connector.h"

#include <thread>

bool sab::Gpg4WinForwardConnectionManager::Initialize()
{
	initialized = true;
	return true;
}

bool sab::Gpg4WinForwardConnectionManager::Start()
{
	if (!initialized)
		return false;
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

void sab::Gpg4WinForwardConnectionManager::Stop()
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

bool sab::Gpg4WinForwardConnectionManager::DelegateConnection(HANDLE connection,
	std::shared_ptr<ProtocolListenerBase> listener, std::shared_ptr<ListenerConnectionData> data, bool isSocket)
{
	auto context = std::make_shared<ForwardIoContext>();

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
	context->contextState = ForwardIoContext::ContextState::HandShake;

	context->owner = shared_from_this();

	{
		std::lock_guard<std::mutex> lg(listMutex);
		contextList.emplace_front(context);
		context->selfIter = contextList.begin();
	}
	DoIoCompletion(context, 0, nullptr);
	return true;
}

void sab::Gpg4WinForwardConnectionManager::SetTarget(std::shared_ptr<ProtocolListenerBase> listener,
	std::wstring target)
{
	targetMap.emplace_back(std::move(listener), std::move(target));
}

void sab::Gpg4WinForwardConnectionManager::RemoveContext(IoContext* context)
{
	std::lock_guard<std::mutex> lg(listMutex);
	contextList.erase(context->selfIter);
}

void sab::Gpg4WinForwardConnectionManager::IocpThreadProc()
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
			else if (GetLastError() == ERROR_BROKEN_PIPE)
			{
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
			auto ctx = std::static_pointer_cast<ForwardIoContext>(context->shared_from_this());

			DoIoCompletion(std::move(ctx), bytes, overlapped);
		}
	}
}

void sab::Gpg4WinForwardConnectionManager::DoIoCompletion(
	std::shared_ptr<ForwardIoContext> context, DWORD transferred,
	LPOVERLAPPED ioOverlapped)
{
	size_t idx = static_cast<size_t>(-1);
	IManagedListener* managedListener;

	switch (context->contextState)
	{
	case ForwardIoContext::ContextState::HandShake:
		LogDebug(L"doing handshake for context ", static_cast<IoContext*>(context.get()));
		managedListener = dynamic_cast<IManagedListener*>(context->listener.get());
		if (!managedListener->DoHandshake(context, transferred))
			return;
		LogDebug(L"handshake done for context ", static_cast<IoContext*>(context.get()));
		if (!PreparePeer(*context))
		{
			LogError(L"cannot connect to target!");
			context->Dispose();
			return;
		}
		for (size_t i = 0; i < ForwardIoContext::PEER_COUNT; ++i)
		{
			if (context->contextState != ForwardIoContext::ContextState::Destroyed)
			{
				DoForward(context, 0, i);
			}
			else
			{
				return;
			}
		}
		context->contextState = ForwardIoContext::ContextState::Established;
		break;
	case ForwardIoContext::ContextState::Established:
		for (size_t i = 0; i < ForwardIoContext::PEER_COUNT; ++i)
		{
			if (&context->overlapped[i] == ioOverlapped)
			{
				idx = i;
				break;
			}
		}
		if (idx == static_cast<size_t>(-1))
		{
			LogDebug(L"unexpected i/o context!");
			context->Dispose();
			return;
		}
		DoForward(std::move(context), transferred, idx);
		break;
	default:
		LogDebug(L"illegal status for pipe context!");
		context->Dispose();
	}
}

void sab::Gpg4WinForwardConnectionManager::DoForward(std::shared_ptr<ForwardIoContext> context, DWORD transferred, size_t peerIdx)
{
	BOOL result;
	switch (context->state[peerIdx])
	{
	case ForwardIoContext::State::Ready:
		result = ReadFile(context->ioHandle[peerIdx], context->buffer[peerIdx],
			MAX_BUFFER_SIZE, NULL, &context->overlapped[peerIdx]);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->Dispose();
			return;
		}
		context->state[peerIdx] = ForwardIoContext::State::Read;
		break;
	case ForwardIoContext::State::Read:
		if (transferred == 0)
		{
			// EOF
			LogDebug(L"end of file on ", context->ioHandle[peerIdx]);
			context->state[peerIdx] = ForwardIoContext::State::Shutdown;
			if (context->ioHandleType[peerIdx] == IoContext::HandleType::SocketHandle)
			{
				shutdown(reinterpret_cast<SOCKET>(context->ioHandle[peerIdx]), SD_RECEIVE);
			}
			if (context->ioHandleType[ForwardIoContext::PeerOf(peerIdx)] == IoContext::HandleType::SocketHandle)
			{
				shutdown(reinterpret_cast<SOCKET>(context->ioHandle[ForwardIoContext::PeerOf(peerIdx)]), SD_SEND);
			}
			break;
		}
		context->needTransfer[peerIdx] = transferred;
		context->bufferOffset[peerIdx] = 0;
		context->state[peerIdx] = ForwardIoContext::State::Write;

		// LogDebug(L"forward ", context->ioHandle[peerIdx], L"->", context->ioHandle[1 - peerIdx], L", ", transferred, L" bytes.");
		
		result = WriteFile(context->ioHandle[ForwardIoContext::PeerOf(peerIdx)],
			context->buffer[peerIdx] + context->bufferOffset[peerIdx],
			static_cast<DWORD>(context->needTransfer[peerIdx]), NULL,
			&context->overlapped[peerIdx]);
		if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
		{
			context->Dispose();
			return;
		}
		break;
	case ForwardIoContext::State::Write:
		context->needTransfer[peerIdx] -= transferred;
		if (context->needTransfer[peerIdx] > 0)
		{
			context->bufferOffset[peerIdx] += transferred;
			result = WriteFile(context->ioHandle[ForwardIoContext::PeerOf(peerIdx)],
				context->buffer[peerIdx] + context->bufferOffset[peerIdx],
				static_cast<DWORD>(context->needTransfer[peerIdx]), NULL,
				&context->overlapped[peerIdx]);
			if (result == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				context->Dispose();
				return;
			}
		}
		else
		{
			// finished sending
			context->state[peerIdx] = ForwardIoContext::State::Ready;
			DoForward(context, 0, peerIdx);
			return;
		}
		break;
	default:
		LogDebug(L"illegal status for pipe context[", peerIdx, L"]!");
		context->Dispose();
		return;
	}

	bool disposeFlag = true;
	for (size_t i = 0; i < ForwardIoContext::PEER_COUNT; ++i)
	{
		disposeFlag = disposeFlag && (context->state[i] == ForwardIoContext::State::Shutdown);
	}
	if (disposeFlag)
	{
		LogDebug(L"finished forward context ", static_cast<IoContext*>(context.get()));
		context->Dispose();
	}
}

bool sab::Gpg4WinForwardConnectionManager::PreparePeer(ForwardIoContext& context)
{
	auto iter = std::find_if(targetMap.begin(), targetMap.end(),
		[&](TargetMapType::value_type& t)
		{
			return t.first == context.listener;
		});
	if (iter == targetMap.end()) {
		LogError(L"connection from unknown listener!");
		return false;
	}

	LogDebug(L"target is ", iter->second);

	using Connector = LibassuanSocketEmulationConnector;
	Connector::HandleType peer = Connector::Connect(iter->second);
	if (peer == Connector::INVALID) {
		LogError(L"cannot open connection to target!");
		return false;
	}
	auto peerHandleGuard = HandleGuard(peer, Connector::Close);

	LogDebug(L"opened target as handle ", reinterpret_cast<HANDLE>(peer));

	if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(peer), iocpHandle,
		reinterpret_cast<ULONG_PTR>(static_cast<IoContext*>(&context)), 0))
	{
		LogDebug(L"cannot associate handle to completion port! ", LogLastError);
		return false;
	}

	peerHandleGuard.release();

	context.ioHandle[0] = context.handle;
	context.ioHandleType[0] = context.handleType;
	context.state[0] = ForwardIoContext::State::Ready;

	context.ioHandle[1] = reinterpret_cast<HANDLE>(peer);
	context.ioHandleType[1] = IoContext::HandleType::SocketHandle;
	context.state[1] = ForwardIoContext::State::Ready;

	return true;
}

sab::ForwardIoContext::ForwardIoContext()
{
	contextState = ContextState::Initialized;
	for (size_t i = 0; i < PEER_COUNT; ++i)
	{
		ioHandle[i] = INVALID_HANDLE_VALUE;
		ioHandleType[i] = HandleType::Invalid;
		memset(&overlapped[i], 0, sizeof(OVERLAPPED));
		state[i] = State::Initialized;
		needTransfer[i] = 0;
		bufferOffset[i] = 0;
	}
}

sab::ForwardIoContext::~ForwardIoContext()
{
	for (size_t i = 0; i < PEER_COUNT; ++i)
	{
		if (ioHandle[i] != INVALID_HANDLE_VALUE)
		{
			CloseIoHandle(ioHandle[i], ioHandleType[i]);
			LogDebug(L"closed handle ", ioHandle[i]);
		}
	}
}

void sab::ForwardIoContext::Dispose()
{
	if (contextState != ContextState::Destroyed) {
		contextState = ContextState::Destroyed;
		for (size_t i = 0; i < PEER_COUNT; ++i)
		{
			if (ioHandle[i] != INVALID_HANDLE_VALUE) {
				CancelIoEx(ioHandle[i], nullptr);
			}
		}
		owner->RemoveContext(this);
	}
}
