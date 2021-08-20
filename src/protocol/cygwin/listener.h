
#pragma once

#include "../listener_base.h"
#include "../connection_manager.h"

namespace sab
{
	class CygwinSocketEmulationListener
		: public ProtocolListenerBase, public IManagedListener
	{
	public:
		static constexpr size_t NONCE_LENGTH = 16;
		static constexpr size_t INFO_LENGTH = 12;

		struct NonceStruct
		{
			uint32_t data[4];
		};

		struct ProcessInfo
		{
			uint32_t pid;
			uint32_t uid;
			uint32_t gid;
		};
	private:
		std::wstring socketPath;
		std::wstring listenAddress;

		char nonce[NONCE_LENGTH];
		ProcessInfo server_info;

		HANDLE cancelEvent = NULL;

		std::shared_ptr<IConnectionManager> connectionManager;

		bool permissionCheckFlag;

	public:
		CygwinSocketEmulationListener(const std::wstring& socketPath,
			const std::wstring& listenAddress,
			std::shared_ptr<IConnectionManager> manager,
			bool permissionCheckFlag);

		bool Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		bool DoHandshake(std::shared_ptr<IoContext> context, int transferred)override;

		~CygwinSocketEmulationListener()override;
	private:
		bool ListenLoop();
	};
}