
#pragma once

#include "../../util.h"
#include "../protocol_ssh_helper.h"
#include "../listener_base.h"
#include "../connection_manager.h"

#include <atomic>
#include <string>
#include <list>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{

	class Win32NamedPipeListener
		: public ProtocolListenerBase, public IManagedListener
	{
	private:
		std::wstring pipePath;

		HANDLE cancelEvent = NULL;

		std::shared_ptr<IConnectionManager> connectionManager;

		bool permissionCheckFlag;
	public:
		Win32NamedPipeListener(const std::wstring& pipePath,
			std::shared_ptr<IConnectionManager> manager,
			bool permissionCheckFlag);

		bool Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		bool DoHandshake(std::shared_ptr<IoContext> context, int transferred)override;

		~Win32NamedPipeListener()override;
	private:
		bool ListenLoop();
	};
}
