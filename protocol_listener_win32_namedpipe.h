
#pragma once

#include "util.h"
#include "protocol_ssh_helper.h"
#include "protocol_listener_base.h"
#include "protocol_listener_iocp_connection_manager.h"

#include <atomic>
#include <string>
#include <list>
#include <mutex>

#include <Windows.h>


namespace sab
{

	class Win32NamedPipeListener
		: public ProtocolListenerBase, public IIocpListener
	{
	private:
		std::wstring pipePath;

		HANDLE cancelEvent = NULL;

		std::shared_ptr<IocpListenerConnectionManager> connectionManager;
	public:
		Win32NamedPipeListener(const std::wstring& pipePath, 
			std::shared_ptr<IocpListenerConnectionManager> manager);

		void Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		bool DoHandshake(std::shared_ptr<IoContext> context, int transferred)override;

		~Win32NamedPipeListener()override;
	private:
		bool ListenLoop();
	};
}
