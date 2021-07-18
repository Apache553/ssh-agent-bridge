
#include "log.h"
#include "util.h"
#include "application.h"
#include "service_support.h"
#include "protocol_listener_libassuan_socket_emu.h"
#include "protocol_listener_win32_namedpipe.h"
#include "protocol_listener_pageant.h"
#include "protocol_listener_unix_domain.h"
#include "protocol_client_pageant.h"
#include "protocol_client_win32_namedpipe.h"

#include <cassert>
#include <sstream>
#include <algorithm>
#include <thread>
#include <vector>

// workaround Windows.h
#undef max

static std::vector<std::wstring> SplitString(const std::wstring& str, wchar_t delim = L' ')
{
	std::vector<std::wstring> ret;
	std::wistringstream iss(str);
	std::wstring tmp;

	while (!iss.eof())
	{
		std::getline(iss, tmp, delim);
		if (!tmp.empty())
			ret.emplace_back(std::move(tmp));
		tmp = {};
	}
	return ret;
}

sab::Application::Application()
{
	cancelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	assert(cancelEvent != nullptr);
}

sab::Application::~Application()
{
}

bool sab::Application::Initialize(const IniFile& config)
{
	const IniSection* general = GetSection(config, L"general");
	if (general == nullptr)
	{
		LogError(L"missing config [general]");
		return false;
	}

	auto listenerName = GetSectionProperty(*general, L"listeners");
	if (listenerName.empty())
	{
		LogError(L"missing config [general].listeners");
		return false;
	}
	auto listenerNameList = SplitString(listenerName);

	auto clientName = GetSectionProperty(*general, L"client");
	if (clientName.empty())
	{
		LogError(L"missing config [general].client");
		return false;
	}

	// validate
	if (std::find(listenerNameList.begin(), listenerNameList.end(), clientName) != listenerNameList.end())
	{
		LogError(L"client ", clientName, " conflicts with listeners");
		return false;
	}
	std::sort(listenerNameList.begin(), listenerNameList.end());
	if (std::unique(listenerNameList.begin(), listenerNameList.end()) != listenerNameList.end())
	{
		LogError(L"duplicated listener dectected");
		return false;
	}

	// do init
	connectionManager = std::make_shared<IocpListenerConnectionManager>();
	dispatcher = std::make_shared<MessageDispatcher>();
	connectionManager->SetEmitMessageCallback([&](sab::SshMessageEnvelope* msg, std::shared_ptr<void> holdKey)
		{
			dispatcher->PostRequest(msg, std::move(holdKey));
		});
	if (!connectionManager->Initialize())
	{
		LogError(L"cannot initialize connection manager");
		return false;
	}
	{
		std::shared_ptr<ProtocolClientBase> p;
		if (clientName == L"pageant")
			p = SetupPageantClient(config);
		else if (clientName == L"namedpipe")
			p = SetupNamedPipeClient(config);
		else
			LogError(L"unknown client name ", clientName);

		if (p == nullptr)
		{
			LogError(L"initialize client ", clientName, " failed");
			return false;
		}
		else
		{
			client = std::move(p);
		}
		dispatcher->SetActiveClient(client);
	}
	for (auto& name : listenerNameList)
	{
		std::shared_ptr<ProtocolListenerBase> p;
		if (name == L"wsl2")
			p = SetupWsl2Listener(config);
		else if (name == L"namedpipe")
			p = SetupNamedPipeListener(config);
		else if (name == L"unix")
			p = SetupUnixListener(config);
		else if (name == L"pageant")
			p = SetupPageantListener(config);
		else
			LogError(L"unknown listener name ", name);

		if (p == nullptr)
		{
			LogError(L"initialize listener ", name, " failed");
			return false;
		}
		else
		{
			listeners.emplace_back(std::move(p));
		}
	}

	return true;
}

int sab::Application::Run()
{
	ServiceSupport::GetInstance().ReportStatus(SERVICE_START_PENDING, 0, 3000);
	auto ini = ParseIniFile(configPath);
	if (!ini.second)
		return 1;

	if (!this->Initialize(ini.first))
		return 1;

	connectionManager->Start();

	std::vector<std::thread> listenThreads;
	for (auto& l : listeners)
	{
		listenThreads.emplace_back([=]()
			{
				if (!l->Run())
				{
					ServiceSupport::GetInstance().ReportStatus(SERVICE_STOPPED, 1);
					exit(1);
				}
			});
	}

	dispatcher->Start();

	ServiceSupport::GetInstance().ReportStatus(SERVICE_RUNNING, 0);

	WaitForSingleObject(cancelEvent, INFINITE);

	ServiceSupport::GetInstance().ReportStatus(SERVICE_STOP_PENDING, 0, 3000);

	dispatcher->Stop();
	for (auto& l : listeners)
	{
		l->Cancel();
	}
	for(auto& l:listenThreads)
	{
		if (l.joinable())
			l.join();
	}
	connectionManager->Stop();

	return 0;
}

int sab::Application::RunStub(bool isService, const std::wstring& configPath)
{
	this->configPath = configPath;
	this->isService = isService;
	this->exitCode = 0;

	if (!isService)
		return Run();

	auto mainProc = [](DWORD, LPWSTR*) {
		auto& t = Application::GetInstance();
		if (!ServiceSupport::GetInstance().RegisterService(ServiceSupport::SERVICE_NAME,
			ServiceControlHandler))
		{
			LogError(L"unable to register service!");
			t.exitCode = 1;
			return;
		}
		t.exitCode = t.Run();
		ServiceSupport::GetInstance().ReportStatus(SERVICE_STOPPED, t.exitCode);
	};

	wchar_t name[sizeof(ServiceSupport::SERVICE_NAME) / sizeof(wchar_t)];
	memcpy(name, ServiceSupport::SERVICE_NAME, sizeof(name));

	SERVICE_TABLE_ENTRYW dispatchTable[] =
	{
		{name,static_cast<LPSERVICE_MAIN_FUNCTIONW>(mainProc)},
		{NULL,NULL}
	};

	if (StartServiceCtrlDispatcherW(dispatchTable) == 0)
	{
		LogError(L"cannot start service! ", LogLastError);
		return 1;
	}
	return exitCode;
}

void sab::Application::Cancel()
{
	SetEvent(cancelEvent);
}

bool sab::Application::IsCancelled()
{
	return WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0;
}

sab::Application& sab::Application::GetInstance()
{
	static Application inst;
	return inst;
}

std::shared_ptr<sab::ProtocolListenerBase> sab::Application::SetupWsl2Listener(const IniFile& config)
{
	const IniSection* section = GetSection(config, L"wsl2");
	if (section == nullptr)
		return nullptr;

	auto socketPath = GetSectionProperty(*section, L"socket-path");
	socketPath = ReplaceEnvironmentVariables(socketPath);
	if (socketPath.empty())
	{
		LogError(L"missing config [wsl2].socket-path");
		return nullptr;
	}
	auto socketAddress = GetSectionProperty(*section, L"socket-address");
	if (socketAddress.empty())
	{
		LogError(L"missing config [wsl2].socket-address");
		return nullptr;
	}
	// TODO: handle permission check config
	return std::make_shared<LibassuanSocketEmulationListener>(socketPath,
		socketAddress, connectionManager);

}

std::shared_ptr<sab::ProtocolListenerBase> sab::Application::SetupNamedPipeListener(const IniFile& config)
{
	const IniSection* section = GetSection(config, L"namedpipe");
	if (section == nullptr)
		return nullptr;

	auto pipePath = GetSectionProperty(*section, L"namedpipe-path");
	pipePath = ReplaceEnvironmentVariables(pipePath);
	if (pipePath.empty())
	{
		LogError(L"missing config [namedpipe].namedpipe-path");
		return nullptr;
	}
	// TODO: handle permission configs
	return std::make_shared<Win32NamedPipeListener>(pipePath, connectionManager);
}

std::shared_ptr<sab::ProtocolListenerBase> sab::Application::SetupPageantListener(const IniFile& config)
{
	// TODO: handle permission configs
	auto ptr = std::make_shared<PageantListener>();
	ptr->SetEmitMessageCallback([&](sab::SshMessageEnvelope* msg, std::shared_ptr<void> holdKey)
		{
			dispatcher->PostRequest(msg, std::move(holdKey));
		});
	return ptr;
}

std::shared_ptr<sab::ProtocolListenerBase> sab::Application::SetupUnixListener(const IniFile& config)
{
	const IniSection* section = GetSection(config, L"unix");
	if (section == nullptr)
		return nullptr;

	auto socketPath = GetSectionProperty(*section, L"socket-path");
	socketPath = ReplaceEnvironmentVariables(socketPath);
	if (socketPath.empty())
	{
		LogError(L"missing config [unix].socket-path");
		return nullptr;
	}
	// TODO: handle permission configs

	return std::make_shared<UnixDomainSocketListener>(socketPath, connectionManager);
}

std::shared_ptr<sab::ProtocolClientBase> sab::Application::SetupPageantClient(const IniFile& config)
{
	return std::make_shared<PageantClient>();
}

std::shared_ptr<sab::ProtocolClientBase> sab::Application::SetupNamedPipeClient(const IniFile& config)
{
	const IniSection* section = GetSection(config, L"namedpipe");
	if (section == nullptr)
		return nullptr;

	auto pipePath = GetSectionProperty(*section, L"namedpipe-path");
	pipePath = ReplaceEnvironmentVariables(pipePath);
	if (pipePath.empty())
	{
		LogError(L"missing config [namedpipe].namedpipe-path");
		return nullptr;
	}
	return std::make_shared<Win32NamedPipeClient>(pipePath);
}

void __stdcall sab::Application::ServiceControlHandler(DWORD dwControl)
{
	switch (dwControl)
	{
	case SERVICE_CONTROL_STOP:
		Application::GetInstance().Cancel();
		break;
	}
	return;
}
