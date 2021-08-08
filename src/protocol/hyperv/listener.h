

#pragma once

#include "../listener_base.h"
#include "../connection_manager.h"
#include "../../lxperm.h"

namespace sab
{
	class HyperVSocketListener
		: public ProtocolListenerBase, public IManagedListener
	{
	private:
		std::wstring listenPort;
		std::wstring listenGUID;
		std::wstring listenServiceTemplate;

		HANDLE cancelEvent = NULL;

		std::shared_ptr<IConnectionManager> connectionManager;
	
	public:
		HyperVSocketListener(const std::wstring& listenPort,
			std::shared_ptr<IConnectionManager> manager,
			const std::wstring& listenGUID,
			const std::wstring& listenServiceTemplate);

		bool Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		bool DoHandshake(std::shared_ptr<IoContext> context, int transferred)override;

		~HyperVSocketListener()override;
	private:
		bool ListenLoop();
	};
}