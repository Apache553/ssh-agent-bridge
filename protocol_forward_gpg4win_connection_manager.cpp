
#include "log.h"
#include "util.h"
#include "protocol_forward_gpg4win_connection_manager.h"
#include "protocol_connector_libassuan_socket_emu.h"

#include <thread>

void GracefullyHup(WSAPOLLFD(&fdList)[2], size_t i, sab::IoContext* context);

bool sab::Gpg4WinForwardConnectionManager::Initialize()
{
	return true;
}

bool sab::Gpg4WinForwardConnectionManager::Start()
{
	return true;
}

void sab::Gpg4WinForwardConnectionManager::Stop()
{
	return;
}

bool sab::Gpg4WinForwardConnectionManager::DelegateConnection(HANDLE connection,
	std::shared_ptr<ProtocolListenerBase> listener, std::shared_ptr<ListenerConnectionData> data, bool isSocket)
{
	auto iter = std::find_if(targetMap.begin(), targetMap.end(),
		[&](TargetMapType::value_type& t)
		{
			return t.first == listener;
		});
	if (iter == targetMap.end()) {
		LogError(L"connection from unknown listener!");
		return false;
	}

	auto s = shared_from_this();
	std::thread thr([=]()
		{
			auto self =
				std::dynamic_pointer_cast<Gpg4WinForwardConnectionManager>(s);
			auto context = std::make_shared<IoContext>();
			context->handle = connection;
			context->isSocket = isSocket;
			context->owner = self;
			context->listener = listener;
			context->listenerData = data;
			HANDLE eventHandle = CreateEventW(NULL, FALSE, FALSE, NULL);
			if (eventHandle == NULL)
			{
				LogError(L"cannot create event handle! ", LogLastError);
				return;
			}
			auto eventGuard = HandleGuard(eventHandle, CloseHandle);
			context->overlapped.hEvent = eventHandle;
			IManagedListener* managedListener = dynamic_cast<IManagedListener*>(listener.get());
			DWORD transferred = 0;
			do
			{
				if (managedListener->DoHandshake(context, transferred))
					break;
				if (WaitForSingleObject(eventHandle, 5000) != WAIT_OBJECT_0)
				{
					// TIMED OUT
					LogError(L"handshake timed out!");
					return;
				}
				if (GetOverlappedResult(context->handle, &context->overlapped, &transferred, FALSE) == FALSE)
				{
					LogError(L"cannot get io operation result! ", GetLastError);
					return;
				}
			} while (true);
			eventGuard.release();
			CloseHandle(eventHandle);

			using Connector = LibassuanSocketEmulationConnector;
			Connector::HandleType peer = Connector::Connect(iter->second);
			if (peer == Connector::INVALID) {
				LogError(L"cannot open connection to target!");
				return;
			}
			auto peerGuard = HandleGuard(peer, closesocket);

			LogDebug(L"started forwarding");
			self->ForwardThreadProc(reinterpret_cast<SOCKET>(connection), peer, context.get());
			LogDebug(L"stopped forwarding");
		});
	thr.detach();
	return true;
}

void sab::Gpg4WinForwardConnectionManager::SetTarget(std::shared_ptr<ProtocolListenerBase> listener,
	std::wstring target)
{
	targetMap.emplace_back(std::move(listener), std::move(target));
}

void sab::Gpg4WinForwardConnectionManager::RemoveContext(IoContext* context)
{
	return;
}

void sab::Gpg4WinForwardConnectionManager::ForwardThreadProc(SOCKET ep1, SOCKET ep2, IoContext* context)
{
	WSAPOLLFD pollFd[2];
	u_long one = 1;
	memset(pollFd, 0, sizeof(pollFd));

	pollFd[0].fd = ep1;
	pollFd[0].events = POLLRDNORM;
	pollFd[1].fd = ep2;
	pollFd[1].events = POLLRDNORM;

	while (true)
	{
		int r = WSAPoll(pollFd, 2, -1);
		if (r < 0)
		{
			LogError(L"WSAPoll failed!", LogWSALastError);
			return;
		}
		else if (r > 0)
		{
			for (size_t i = 0; i < 2; ++i)
			{
				if (pollFd[i].revents & POLLERR)
				{
					LogError(L"error on socket: ", pollFd[i].fd);
					return;
				}
				else if (pollFd[i].revents & POLLIN)
				{
					// read some then write to another fd
					int rr = recv(pollFd[i].fd, context->ioBuffer, MAX_BUFFER_SIZE, 0);
					if (rr < 0)
					{
						LogError(L"recv() failed! ", LogWSALastError);
						return;
					}
					else if (rr == 0)
					{
						GracefullyHup(pollFd, i, context);
						return;
					}
					else if ((rr > 0) && !SendBuffer(pollFd[1 - i].fd, context->ioBuffer, rr))
					{
						return;
					}
				}
				else if (pollFd[i].revents & POLLHUP)
				{
					// close sockets gracefully
					GracefullyHup(pollFd, i, context);
					return;
				}
			}
		}
	}

}

static void GracefullyHup(WSAPOLLFD(&fdList)[2], size_t i, sab::IoContext* context)
{
	//std::cout << "gracefully close sockets~\n";
	int rr;
	do {
		rr = recv(fdList[i].fd, context->ioBuffer, sab::MAX_BUFFER_SIZE, 0);
		if ((rr > 0) && !sab::SendBuffer(fdList[1 - i].fd, context->ioBuffer, rr))
			return;
	} while (rr > 0);
	shutdown(fdList[i].fd, SD_RECEIVE);
	shutdown(fdList[1 - i].fd, SD_SEND);
	do
	{
		rr = recv(fdList[1 - i].fd, context->ioBuffer, sab::MAX_BUFFER_SIZE, 0);
		if ((rr > 0) && !sab::SendBuffer(fdList[i].fd, context->ioBuffer, rr))
			return;
	} while (rr > 0);
	shutdown(fdList[1 - i].fd, SD_RECEIVE);
	shutdown(fdList[i].fd, SD_SEND);
	return;
}