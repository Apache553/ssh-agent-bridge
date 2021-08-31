
#include "log.h"
#include "util.h"
#include "application.h"
#include "service_support.h"
#include "protocol/libassuan_socket_emulation/listener.h"
#include "protocol/namedpipe/listener.h"
#include "protocol/pageant/listener.h"
#include "protocol/unix/listener.h"
#include "protocol/hyperv/listener.h"
#include "protocol/cygwin/listener.h"
#include "protocol/namedpipe/client.h"
#include "protocol/pageant/client.h"
#include "lxperm.h"
#include "cmdline_option.h"

#include <cassert>
#include <sstream>
#include <algorithm>
#include <thread>
#include <vector>
#include <functional>

// workaround Windows.h
#undef max

struct TypeAction
{
	std::wstring name;
	std::function<
		std::shared_ptr<sab::ProtocolListenerBase>(
			const sab::IniSection&,
			std::shared_ptr<sab::IConnectionManager>,
			std::shared_ptr<sab::MessageDispatcher>
			)
	> createListener;
	std::function<
		std::shared_ptr<sab::ProtocolClientBase>(
			const sab::IniSection&
			)
	> createClient;
};

static TypeAction actionList[] = {
	{L"namedpipe", sab::SetupNamedPipeListener, sab::SetupNamedPipeClient },
	{L"pageant", sab::SetupPageantListener, sab::SetupPageantClient },
	{L"assuan_emu", sab::SetupWsl2Listener },
	{L"unix", sab::SetupUnixListener },
	{L"hyperv", sab::SetupHyperVListener },
	{L"cygwin", sab::SetupCygwinListener }
};

static std::vector<std::wstring> forwardEnabledList = {
	L"unix",
	L"assuan_emu",
	L"hyperv",
	L"cygwin"
};

static bool GetLxPermissionInfo(const sab::IniSection& section, sab::LxPermissionInfo& perm)
{
	const wchar_t* fieldName;
	try {
		fieldName = L"metadata-uid";
		auto str = sab::GetPropertyString(section, fieldName);
		if (str.second && !str.first.empty())
			perm.uid = std::stoi(str.first, nullptr, 0);
		fieldName = L"metadata-gid";
		str = sab::GetPropertyString(section, fieldName);
		if (str.second && !str.first.empty())
			perm.gid = std::stoi(str.first, nullptr, 0);
		fieldName = L"metadata-mode";
		str = sab::GetPropertyString(section, fieldName);
		if (str.second && !str.first.empty())
			perm.mode = std::stoi(str.first, nullptr, 0);
	}
	catch (std::invalid_argument)
	{
		LogError(L"invalid value for ", fieldName);
		return false;
	}
	catch (std::out_of_range)
	{
		LogError(L"value too large for ", fieldName);
		return false;
	}
	return true;
}

sab::Application::Application()
	:exitCode(0), isService(false)
{
	cancelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	assert(cancelEvent != NULL);
}

sab::Application::~Application()
{
	CloseHandle(cancelEvent);
}

bool sab::Application::Initialize(const IniFile& config)
{
	// do init
	connectionManager = std::make_shared<ProxyConnectionManager>();
	gpgConnectionManager = std::make_shared<Gpg4WinForwardConnectionManager>();
	dispatcher = std::make_shared<MessageDispatcher>();
	connectionManager->SetEmitMessageCallback([=](sab::SshMessageEnvelope* msg, std::shared_ptr<void> holdKey)
		{
			dispatcher->PostRequest(msg, std::move(holdKey));
		});
	if (!connectionManager->Initialize())
	{
		LogError(L"cannot initialize connection manager");
		return false;
	}
	if (!gpgConnectionManager->Initialize())
	{
		LogError(L"cannot initialize gpg connection manager");
		return false;
	}

	bool clientSetFlag = false;
	for (const auto& s : config)
	{
		const std::wstring& sectionName = s.first;
		const IniSection& section = s.second;
		if (sectionName == L"general")
		{
			Logger::LogLevel logLevel = Logger::LogLevel::Invalid;
			auto loglevelStr = GetPropertyString(section, L"loglevel");
			if (loglevelStr.second) {
				if (EqualStringIgnoreCase(loglevelStr.first, L"debug"))
					logLevel = Logger::LogLevel::Debug;
				else if (EqualStringIgnoreCase(loglevelStr.first, L"info"))
					logLevel = Logger::LogLevel::Info;
				else if (EqualStringIgnoreCase(loglevelStr.first, L"warn"))
					logLevel = Logger::LogLevel::Warning;
				else if (EqualStringIgnoreCase(loglevelStr.first, L"error"))
					logLevel = Logger::LogLevel::Error;
				if (logLevel == Logger::LogLevel::Invalid)
				{
					LogError(L"invalid loglevel!");
					return false;
				}
				Logger::GetInstance().SetLogOutputLevel(logLevel);
			}
		}
		else
		{
			constexpr size_t actionListSize = std::extent<decltype(actionList)>::value;
			auto type = GetPropertyString(section, L"type");
			auto role = GetPropertyString(section, L"role");
			if (!type.second || !role.second)
			{
				LogError(L"section \"", sectionName, "\" has no type and role property!");
				return false;
			}
			bool findFlag = false;
			for (size_t i = 0; i < actionListSize; ++i)
			{
				if (type.first == actionList[i].name)
				{
					findFlag = true;
					if (role.first == L"listener")
					{
						LogDebug(L"Setting up listener \"", sectionName, L"\" type \"", type.first, L"\"");
						if (!actionList[i].createListener)
						{
							LogError(L"type \"", type.first, L"\" does not support role \"", role.first, L"\"");
							return false;
						}
						// check gpg forward
						auto targetPath = GetPropertyString(section, L"forward-socket-path");
						std::shared_ptr<ProtocolListenerBase> ptr;
						if (targetPath.second)
						{
							bool forwardEnabled = std::find(forwardEnabledList.begin(), forwardEnabledList.end(), type.first) != forwardEnabledList.end();
							if (forwardEnabled) {
								LogDebug(L"Setup for gpg forwarding.");
								ptr = actionList[i].createListener(section, gpgConnectionManager, dispatcher);
								auto targetPathEx = ReplaceEnvironmentVariables(targetPath.first);
								if (ptr) {
									gpgConnectionManager->SetTarget(ptr, targetPathEx);
									// auto sockPath = GetPropertyString(section, L"path");
									// LogDebug(L"\"", targetPathEx, L"\" <-> \"", ReplaceEnvironmentVariables(sockPath.first), L"\"");
								}
							}
							else
							{
								LogError(L"gpg forwarding for this type is not supported!");
								return false;
							}
						}
						else {
							LogDebug(L"Setup for ssh agent proxy.");
							ptr = actionList[i].createListener(section, connectionManager, dispatcher);
						}
						if (ptr == nullptr)
						{
							LogError(L"cannot create listener, check your config!");
							return false;
						}
						listeners.emplace_back(std::move(ptr));
					}
					else if (role.first == L"client")
					{
						LogDebug(L"Setting up client \"", sectionName, L"\" type \"", type.first, L"\"");
						if (!actionList[i].createClient)
						{
							LogError(L"type \"", type.first, L"\" does not support role \"", role.first, L"\"");
							return false;
						}
						auto ptr = actionList[i].createClient(section);
						if (ptr == nullptr)
						{
							return false;
						}
						clientSetFlag = true;
						dispatcher->AddClient(ptr);
					}
					else
					{
						LogError(L"section \"", sectionName, L"\" has invalid role!");
						return false;
					}
				}
			}
			if (!findFlag)
			{
				LogError(L"unknown type \"", type.first, L"\"!");
				return false;
			}
		}
	}
	if (!clientSetFlag)
	{
		LogError(L"no client set!");
		return false;
	}

	return true;
}

int sab::Application::Run()
{
	int exitCode = 0;
	ServiceSupport::GetInstance().ReportStatus(SERVICE_START_PENDING, exitCode, 3000);
	auto ini = ParseIniFile(configPath);
	if (!ini.second)
		return 1;

	if (!this->Initialize(ini.first))
		return 1;

	connectionManager->Start();
	gpgConnectionManager->Start();

	std::vector<std::thread> listenThreads;
	HANDLE errorEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	assert(errorEvent != NULL);
	auto errorEventGuard = HandleGuard(errorEvent, CloseHandle);

	for (auto& l : listeners)
	{
		listenThreads.emplace_back([=]()
			{
				if (!l->Run())
				{
					LogError(L"cannot start listener!");
					SetEvent(errorEvent);
				}
			});
	}

	HANDLE waitList[2]{ cancelEvent, errorEvent };

	dispatcher->Start();

	ServiceSupport::GetInstance().ReportStatus(SERVICE_RUNNING, exitCode);

	DWORD result = WaitForMultipleObjects(2, waitList, FALSE, INFINITE);
	if (result != WAIT_OBJECT_0)
	{
		LogError(L"error occurred, exiting...");
		exitCode = 1;
	}
	ServiceSupport::GetInstance().ReportStatus(SERVICE_STOP_PENDING, exitCode, 3000);

	dispatcher->Stop();
	for (auto& l : listeners)
	{
		l->Cancel();
	}
	for (auto& l : listenThreads)
	{
		if (l.joinable())
			l.join();
	}
	connectionManager->Stop();
	gpgConnectionManager->Stop();
	return exitCode;
}

int sab::Application::RunStub(bool isService, const std::wstring& configPath)
{
	this->configPath = configPath;
	this->isService = isService;
	this->exitCode = 0;

	if (!isService)
		return Run();

	auto mainProc = [](DWORD argc, LPWSTR* argv) {
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

void __stdcall sab::Application::ServiceControlHandler(DWORD dwControl)
{
	switch (dwControl)
	{
	case SERVICE_CONTROL_STOP:
		ServiceSupport::GetInstance().ReportStatus(SERVICE_STOP_PENDING, 0, 3000);
		Application::GetInstance().Cancel();
		break;
	}
	return;
}

std::shared_ptr<sab::ProtocolListenerBase> sab::SetupWsl2Listener(
	const IniSection& section,
	std::shared_ptr<IConnectionManager> manager,
	std::shared_ptr<MessageDispatcher> dispatcher)
{
	auto socketPath = GetPropertyString(section, L"path");
	auto socketAddress = GetPropertyString(section, L"listen-address");
	auto writeMetadata = GetPropertyBoolean(section, L"write-lxss-metadata");
	auto enablePermissionCheck = GetPropertyBoolean(section, L"enable-permission-check");
	LxPermissionInfo perm;

	if (!socketPath.second)
		return nullptr;

	if (!socketAddress.second)
		socketAddress.first = L"0.0.0.0";

	if (!writeMetadata.second)
		writeMetadata.first = false;

	if (!enablePermissionCheck.second)
		enablePermissionCheck.first = true;

	if (writeMetadata.first && !GetLxPermissionInfo(section, perm))
		return nullptr;

	socketPath.first = ReplaceEnvironmentVariables(socketPath.first);

	return std::make_shared<LibassuanSocketEmulationListener>(
		socketPath.first,
		socketAddress.first,
		manager,
		enablePermissionCheck.first,
		writeMetadata.first,
		perm);
}

std::shared_ptr<sab::ProtocolListenerBase> sab::SetupNamedPipeListener(
	const IniSection& section,
	std::shared_ptr<IConnectionManager> manager,
	std::shared_ptr<MessageDispatcher> dispatcher)
{
	auto socketPath = GetPropertyString(section, L"path");
	auto enablePermissionCheck = GetPropertyBoolean(section, L"enable-permission-check");
	LxPermissionInfo perm;

	if (!socketPath.second)
		return nullptr;

	if (!enablePermissionCheck.second)
		enablePermissionCheck.first = true;

	socketPath.first = ReplaceEnvironmentVariables(socketPath.first);

	return std::make_shared<Win32NamedPipeListener>(
		socketPath.first,
		manager,
		enablePermissionCheck.first);
}

std::shared_ptr<sab::ProtocolListenerBase> sab::SetupPageantListener(
	const IniSection& section,
	std::shared_ptr<IConnectionManager> manager,
	std::shared_ptr<MessageDispatcher> dispatcher)
{
	auto enablePermissionCheck = GetPropertyBoolean(section, L"enable-permission-check");
	auto allowNonelevatedAccess = GetPropertyBoolean(section, L"allow-non-elevated-access");

	if (!enablePermissionCheck.second)
		enablePermissionCheck.first = true;

	if (!allowNonelevatedAccess.second)
		allowNonelevatedAccess.first = false;

	auto ptr = std::make_shared<PageantListener>(
		enablePermissionCheck.first,
		allowNonelevatedAccess.first);

	if (ptr)
	{
		ptr->SetEmitMessageCallback([=](sab::SshMessageEnvelope* msg, std::shared_ptr<void> holdKey)
			{
				dispatcher->PostRequest(msg, std::move(holdKey));
			});
	}

	return ptr;

}

std::shared_ptr<sab::ProtocolListenerBase> sab::SetupUnixListener(
	const IniSection& section,
	std::shared_ptr<IConnectionManager> manager,
	std::shared_ptr<MessageDispatcher> dispatcher)
{
	auto socketPath = GetPropertyString(section, L"path");
	auto writeMetadata = GetPropertyBoolean(section, L"write-lxss-metadata");
	auto enablePermissionCheck = GetPropertyBoolean(section, L"enable-permission-check");
	LxPermissionInfo perm;

	if (!socketPath.second)
		return nullptr;

	if (!writeMetadata.second)
		writeMetadata.first = false;

	if (!enablePermissionCheck.second)
		enablePermissionCheck.first = true;

	if (writeMetadata.first && !GetLxPermissionInfo(section, perm))
		return nullptr;

	socketPath.first = ReplaceEnvironmentVariables(socketPath.first);

	return std::make_shared<UnixDomainSocketListener>(
		socketPath.first,
		manager,
		enablePermissionCheck.first,
		writeMetadata.first,
		perm);
}

std::shared_ptr<sab::ProtocolListenerBase> sab::SetupHyperVListener(const IniSection& section, std::shared_ptr<IConnectionManager> manager, std::shared_ptr<MessageDispatcher> dispatcher)
{
	auto listenGUID = GetPropertyString(section, L"listen-address");
	auto listenPort = GetPropertyString(section, L"listen-port");
	auto listenServiceTemplate = GetPropertyString(section, L"listen-service-template");
	return std::make_shared<HyperVSocketListener>(listenPort.first, manager, listenGUID.first, listenServiceTemplate.first);
}

std::shared_ptr<sab::ProtocolListenerBase> sab::SetupCygwinListener(const IniSection& section, std::shared_ptr<IConnectionManager> manager, std::shared_ptr<MessageDispatcher> dispatcher)
{
	auto socketPath = GetPropertyString(section, L"path");

	auto enablePermissionCheck = GetPropertyBoolean(section, L"enable-permission-check");
	LxPermissionInfo perm;

	if (!socketPath.second)
		return nullptr;

	if (!enablePermissionCheck.second)
		enablePermissionCheck.first = true;

	socketPath.first = ReplaceEnvironmentVariables(socketPath.first);

	return std::make_shared<CygwinSocketEmulationListener>(
		socketPath.first,
		L"127.0.0.1",
		manager,
		enablePermissionCheck.first);
}

std::shared_ptr<sab::ProtocolClientBase> sab::SetupPageantClient(const IniSection& section)
{
	auto restrictProcessName = GetPropertyString(section, L"restrict-process");
	return std::make_shared<PageantClient>(restrictProcessName.first);
}

std::shared_ptr<sab::ProtocolClientBase> sab::SetupNamedPipeClient(const IniSection& section)
{
	auto socketPath = GetPropertyString(section, L"path");

	if (!socketPath.second)
		return nullptr;

	socketPath.first = ReplaceEnvironmentVariables(socketPath.first);

	return std::make_shared<Win32NamedPipeClient>(
		socketPath.first);
}
