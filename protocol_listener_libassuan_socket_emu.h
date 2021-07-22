
#pragma once

#include "protocol_listener_base.h"
#include "protocol_iocp_connection_manager.h"
#include "lxperm.h"

namespace sab
{
	class LibassuanSocketEmulationListener
		: public ProtocolListenerBase, public IManagedListener
	{
	public:
		static constexpr size_t NONCE_LENGTH = 16;
	private:
		std::wstring socketPath;
		std::wstring listenAddress;

		char nonce[NONCE_LENGTH];
		
		HANDLE cancelEvent = NULL;

		std::shared_ptr<IConnectionManager> connectionManager;

		bool permissionCheckFlag;
		bool writeWslMetadataFlag;
		LxPermissionInfo perm;
	public:
		LibassuanSocketEmulationListener(const std::wstring& socketPath,
			const std::wstring& listenAddress,
			std::shared_ptr<IConnectionManager> manager,
			bool permissionCheckFlag,
			bool writeWslMetadataFlag,
			const LxPermissionInfo& perm);

		bool Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		bool DoHandshake(std::shared_ptr<IoContext> context, int transferred)override;

		~LibassuanSocketEmulationListener()override;
	private:
		bool ListenLoop();
	};
}