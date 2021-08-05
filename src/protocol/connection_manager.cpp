
#include "../log.h"
#include "../util.h"
#include "connection_manager.h"

#include <cstring>
#include <WinSock2.h>

sab::IoContext::IoContext()
	:handle(INVALID_HANDLE_VALUE),
	handleType(HandleType::FileHandle)
{
	LogDebug(L"created io context: ", this);
}

sab::IoContext::~IoContext()
{
	LogDebug(L"destruct io context:", this);
}

void sab::IoContext::CloseIoHandle(HANDLE handle, HandleType type)
{
	switch (type)
	{
	case HandleType::FileHandle:
		CloseHandle(handle);
	case HandleType::SocketHandle:
		closesocket(reinterpret_cast<SOCKET>(handle));
	}
}
