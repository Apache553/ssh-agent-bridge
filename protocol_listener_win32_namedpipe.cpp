
#include "protocol_listener_win32_namedpipe.h"

#include <cstring>
#include <stdexcept>

#include <sddl.h>

sab::Win32NamedPipeListener::Win32NamedPipeListener(
	const std::wstring& pipePath
)
	:pipePath(pipePath)
{
	cancelEvent = CreateEventW(NULL, TRUE,
		FALSE, NULL);
}

void sab::Win32NamedPipeListener::Run()
{
	HANDLE pipeHandle;
	HANDLE waitHandles[2];

	SECURITY_ATTRIBUTES sa;
	const wchar_t pipeSDDL[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x12019b;;;AU)";

	std::memset(&sa, 0, sizeof(SECURITY_ATTRIBUTES));
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		pipeSDDL,
		SDDL_REVISION_1,
		&sa.lpSecurityDescriptor,
		&sa.nLength)) 
	{
		
	}

	while (true)
	{
		pipeHandle = CreateNamedPipeW(
			pipePath.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
			PIPE_UNLIMITED_INSTANCES,
			MAX_PIPE_BUFFER_SIZE,
			MAX_PIPE_BUFFER_SIZE,
			0,
			&sa);
	}
}

void sab::Win32NamedPipeListener::Cancel()
{
}

bool sab::Win32NamedPipeListener::IsCancelled() const
{
	return false;
}

sab::Win32NamedPipeListener::~Win32NamedPipeListener()
{
	if (cancelEvent)
		CloseHandle(cancelEvent);
}
