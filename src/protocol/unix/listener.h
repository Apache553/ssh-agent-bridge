
#pragma once

#include "../listener_base.h"
#include "../connection_manager.h"
#include "../../lxperm.h"

namespace sab
{
	class UnixDomainSocketListener
		: public ProtocolListenerBase, public IManagedListener
	{
	private:
		std::wstring socketPath;

		HANDLE cancelEvent = NULL;

		std::shared_ptr<IConnectionManager> connectionManager;

		bool permissionCheckFlag;
		bool writeWslMetadataFlag;

		LxPermissionInfo perm;
	public:
		UnixDomainSocketListener(const std::wstring& socketPath,
			std::shared_ptr<IConnectionManager> manager,
			bool permissionCheckFlag,
			bool writeWslMetadataFlag,
			const LxPermissionInfo& perm);

		bool Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		bool DoHandshake(std::shared_ptr<IoContext> context, int transferred)override;

		~UnixDomainSocketListener()override;
	private:
		bool ListenLoop();
	};
}