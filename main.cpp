
#include "log.h"
#include "util.h"
#include "application.h"
#include "cmdline_option.h"
#include "service_support.h"

#include <cassert>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	PWSTR pCmdLine, int nCmdShow)
{
	// MessageBoxW(NULL, L"attach debugger", L"info", MB_OK);
	sab::CommandLineOption options =
		sab::ExtractCommandLineOption(
			sab::SplitCommandLine(
				GetCommandLineW()));

	if (!options.isGood)
		return 1;

	sab::Logger::GetInstance(options.isDebug);

	if (options.logLevel == sab::Logger::LogLevel::Invalid)
		options.logLevel = sab::Logger::LogLevel::Info;
	sab::Logger::GetInstance().SetLogOutputLevel(options.logLevel);

	if (options.isIntallService)
	{
		bool r = sab::ServiceSupport::InstallService();
		MessageBoxW(NULL,
			r ? L"Successfully Installed." : L"Failed to install!",
			L"Info",
			r ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONERROR));
		return 0;
	}
	else if (options.isUninstallService)
	{
		bool r = sab::ServiceSupport::UninstallService();
		MessageBoxW(NULL,
			r ? L"Successfully uninstalled." : L"Failed to uninstall!",
			L"Info",
			r ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONERROR));
		return 0;
	}

	if (options.configPath.empty())
		options.configPath = sab::GetDefaultConfigPath();
	if (options.configPath.empty())
	{
		LogError(L"no config file!");
		return 1;
	}
	LogInfo(L"using config from ", options.configPath);

	WORD versionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	if (WSAStartup(versionRequested, &wsaData) != 0)
	{
		LogError(L"WSAStartup failed! ", LogWSALastError);
		return 1;
	}

	int ret = sab::Application::GetInstance().RunStub(options.isService, options.configPath);

	WSACleanup();
	return ret;
}