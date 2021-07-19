
#pragma once

#include "ini_parse.h"
#include "protocol_listener_base.h"
#include "protocol_listener_iocp_connection_manager.h"
#include "protocol_client_base.h"
#include "message_dispatcher.h"

#include <memory>

namespace sab
{
	class Application
	{
	private:
		std::vector<std::shared_ptr<ProtocolListenerBase>> listeners;
		std::shared_ptr<IocpListenerConnectionManager> connectionManager;
		std::shared_ptr<ProtocolClientBase> client;
		std::shared_ptr<MessageDispatcher> dispatcher;

		std::wstring configPath;
		bool isService;
		int exitCode;

		HANDLE cancelEvent;
	public:

		int Run();
		int RunStub(bool isService, const std::wstring& configPath);
		void Cancel();
		bool IsCancelled();

		static Application& GetInstance();

		~Application();
	private:
		Application();

		bool Initialize(const IniFile& config);

		std::shared_ptr<ProtocolListenerBase> SetupWsl2Listener(const IniFile& config);
		std::shared_ptr<ProtocolListenerBase> SetupNamedPipeListener(const IniFile& config);
		std::shared_ptr<ProtocolListenerBase> SetupPageantListener(const IniFile& config);
		std::shared_ptr<ProtocolListenerBase> SetupUnixListener(const IniFile& config);

		std::shared_ptr<ProtocolClientBase> SetupPageantClient(const IniFile& config);
		std::shared_ptr<ProtocolClientBase> SetupNamedPipeClient(const IniFile& config);

		static void __stdcall ServiceControlHandler(DWORD dwControl);

	};
}