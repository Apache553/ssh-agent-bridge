
#pragma once


#include "../connection_manager.h"

#include <string>
#include <vector>

#include <WinSock2.h>

namespace sab
{
	class ForwardIoContext
		:public IoContext
	{
	public:
		static constexpr int PEER_COUNT = 2;
		static size_t PeerOf(size_t idx) { return 1 - idx; }

		enum class ContextState
		{
			Initialized = 0,
			HandShake,
			Established,
			Destroyed
		};

		enum class State
		{
			Initialized = 0,
			Ready,
			Read,
			Write,
			Shutdown
		};

		ContextState contextState;

		HANDLE ioHandle[PEER_COUNT];
		HandleType ioHandleType[PEER_COUNT];
		OVERLAPPED overlapped[PEER_COUNT];
		State state[PEER_COUNT];
		char buffer[PEER_COUNT][MAX_BUFFER_SIZE];
		ptrdiff_t needTransfer[PEER_COUNT];
		ptrdiff_t bufferOffset[PEER_COUNT];

		ForwardIoContext();
		~ForwardIoContext();

		void Dispose() override;
	};

	class Gpg4WinForwardConnectionManager
		:public IConnectionManager
	{
	public:
		using TargetMapType = std::vector<
			std::pair<std::shared_ptr<ProtocolListenerBase>, std::wstring>>;

	private:
		TargetMapType targetMap;

		std::atomic<bool> cancelFlag;

		HANDLE iocpHandle;

		std::thread iocpThread;

		std::list<std::shared_ptr<IoContext>> contextList;

		bool initialized;

		std::mutex listMutex;
	public:
		bool Initialize() override;
		bool Start() override;
		void Stop() override;

		bool DelegateConnection(HANDLE connection, std::shared_ptr<ProtocolListenerBase> listener,
			std::shared_ptr<ListenerConnectionData> data, bool isSocket) override;

		void SetTarget(std::shared_ptr<ProtocolListenerBase> listener,
			std::wstring target);

		~Gpg4WinForwardConnectionManager() = default;

		void RemoveContext(IoContext* context) override;
	private:
		void IocpThreadProc();

		void DoIoCompletion(std::shared_ptr<ForwardIoContext> context, DWORD transferred, LPOVERLAPPED ioOverlapped);
		void DoForward(std::shared_ptr<ForwardIoContext> context, DWORD transferred, size_t peerIdx);
		bool PreparePeer(ForwardIoContext& context);
	};
}
