
#pragma once

#include <string>

#include "protocol_connection_manager.h"

#include <vector>

#include <WinSock2.h>

namespace sab
{
	class Gpg4WinForwardConnectionManager
		:public IConnectionManager
	{
	public:
		using TargetMapType = std::vector<
			std::pair<std::shared_ptr<ProtocolListenerBase>, std::wstring>>;

	private:
		TargetMapType targetMap;
	public:
		bool Initialize() override;
		bool Start() override;
		void Stop() override;

		bool DelegateConnection(HANDLE connection, std::shared_ptr<ProtocolListenerBase> listener,
			std::shared_ptr<ListenerConnectionData> data, bool isSocket) override;

		void SetTarget(std::shared_ptr<ProtocolListenerBase> listener,
			std::wstring target);

		~Gpg4WinForwardConnectionManager() = default;
	protected:
		void RemoveContext(IoContext* context) override;
	private:
		void ForwardThreadProc(SOCKET ep1, SOCKET ep2, IoContext* context);
	};
}
