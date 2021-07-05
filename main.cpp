
#include <Windows.h>


#include <cstdio>
#include <clocale>
#include <condition_variable>

#include "log.h"
#include "protocol_ssh_helper.h"
#include "protocol_listener_win32_namedpipe.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
	PWSTR pCmdLine, int nCmdShow)
{
	sab::Logger::GetInstance(true);

	LogDebug(L"Debug");
	LogInfo(L"Info");
	LogWarning(L"Warning");
	LogError(L"Error");
	LogFatal(L"Fatal");
	return 0;
}