
#include "log.h"
#include "util.h"
#include "protocol_ssh_helper.h"
#include "protocol_listener_iocp_connection_manager.h"
#include "protocol_listener_win32_namedpipe.h"
#include "protocol_listener_unix_domain.h"
#include "protocol_listener_pageant.h"
#include "protocol_client_pageant.h"
#include "protocol_client_win32_namedpipe.h"
#include "message_dispatcher.h"

#include <cstdio>
#include <clocale>
#include <condition_variable>
#include <cassert>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	PWSTR pCmdLine, int nCmdShow)
{
	sab::Logger::GetInstance(true);
	sab::Logger::GetInstance().SetLogOutputLevel(sab::Logger::Debug);

	WORD versionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	if (WSAStartup(versionRequested, &wsaData) != 0)
	{
		LogError(L"WSAStartup failed! ", LogWSALastError);
		return 1;
	}

	auto dispatcher = std::make_shared<sab::MessageDispatcher>();
	auto win32NamedPipeClient = std::make_shared<sab::Win32NamedPipeClient>(LR"_(\\.\pipe\openssh-ssh-agent)_");
	dispatcher->SetActiveClient(win32NamedPipeClient);

	auto connectionManager = std::make_shared<sab::IocpListenerConnectionManager>();
	//auto win32listener =std::make_shared<sab::Win32NamedPipeListener>(LR"_(\\.\pipe\openssh-ssh-agent)_", connectionManager);
	auto unixListener = std::make_shared<sab::UnixDomainSocketListener>(LR"_(D:\ssh-agent.socket)_", connectionManager);
	//auto client = std::make_shared<sab::PageantClient>();

	if (!connectionManager->Initialize())
		return 1;
	connectionManager->SetEmitMessageCallback([&](sab::SshMessageEnvelope* msg, std::shared_ptr<void> holdKey)
		{
			dispatcher->PostRequest(msg, holdKey);
		});
	if (!connectionManager->Start())
		return 1;
	std::thread unixListenerThread([&]()
		{
			unixListener->Run();
		});


	auto pageantListener = std::make_shared<sab::PageantListener>();
	pageantListener->SetEmitMessageCallback([&](sab::SshMessageEnvelope* msg, std::shared_ptr<void> holdKey)
		{
			dispatcher->PostRequest(msg, holdKey);
		});
	std::thread pageantListenerThread([&]() {
		pageantListener->Run();
		});



	dispatcher->Start();
	HANDLE exitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	assert(exitEvent != NULL);
	WaitForSingleObject(exitEvent, INFINITE);
	dispatcher->Stop();
	pageantListener->Cancel();
	unixListener->Cancel();
	connectionManager->Stop();
	WSACleanup();
	return 0;
}