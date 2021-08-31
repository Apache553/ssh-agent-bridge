
#include "../../log.h"
#include "../../util.h"
#include "client.h"

#include <wil/resource.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>
#include <WinSock2.h>

static bool FilterWindowProcess(HWND windowHandle, const std::wstring& processName)
{
	DWORD processId;

	GetWindowThreadProcessId(windowHandle, &processId);

	// eliminate our self
	if (GetCurrentProcessId() == processId)
		return false;

	if (processName.empty())
		return true;

	wil::unique_handle processHandle;
	processHandle.reset(
		OpenProcess(
			PROCESS_QUERY_LIMITED_INFORMATION,
			FALSE,
			processId));
	if (!processHandle.is_valid())
		return false;

	DWORD bufferSize = MAX_PATH;
	auto buffer = std::make_unique<wchar_t[]>(bufferSize);
	BOOL result;
	do {
		result = QueryFullProcessImageNameW(
			processHandle.get(),
			PROCESS_NAME_NATIVE,
			buffer.get(),
			&bufferSize);
		if (!result)
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				bufferSize += bufferSize / 2;
				buffer = std::make_unique<wchar_t[]>(bufferSize);
			}
			else
			{
				return false;
			}
		}
	} while (!result);

	wchar_t* lastSlash = nullptr;
	for (wchar_t* iter = buffer.get(); *iter != L'\0'; ++iter)
	{
		if (*iter == L'\\')
			lastSlash = iter;
	}
	if (lastSlash == nullptr)
		return false;
	++lastSlash;

	if (sab::EqualStringIgnoreCase(processName, lastSlash))
		return true;

	return false;
}

static HWND GetPageantWindowHandle(const std::wstring& processName)
{
	HWND result = NULL;
	while ((result = FindWindowExW(NULL, result, L"Pageant", L"Pageant")))
	{
		if (FilterWindowProcess(result, processName))
			return result;
	}
	result = NULL;
	while ((result = FindWindowExW(HWND_MESSAGE, result, L"Pageant", L"Pageant")))
	{
		if (FilterWindowProcess(result, processName))
			return result;
	}
	return NULL;
}

sab::PageantClient::PageantClient(const std::wstring& processName)
	:processName(processName)
{
}

sab::PageantClient::~PageantClient()
{
}

bool sab::PageantClient::SendSshMessage(SshMessageEnvelope* message)
{
	std::ostringstream oss;

	if (message->length + HEADER_SIZE > MAX_PAGEANT_MESSAGE_SIZE)
	{
		LogDebug(L"message too long!");
		return false;
	}

	HWND pageantWindow = GetPageantWindowHandle(processName);

	if (pageantWindow == NULL)
	{
		LogDebug(L"cannot find Pageant communication window! ", LogLastError);
		return false;
	}

	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);

	std::wostringstream sddlStream;
	PSECURITY_DESCRIPTOR sd;
	std::wstring sid = GetCurrentUserSidString();

	if (sid.empty())
	{
		return false;
	}

	// set owner
	sddlStream << L"O:" << sid;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		sddlStream.str().c_str(),
		SDDL_REVISION_1,
		&sd,
		NULL))
	{
		LogError(L"cannot convert sddl to security descriptor!");
		return false;
	}
	auto sdGuard = HandleGuard(sd, LocalFree);

	sa.lpSecurityDescriptor = sd;
	sa.bInheritHandle = FALSE;

	oss << "PageantRequest" << std::hex << std::setfill('0') << std::setw(8)
		<< (uint32_t)GetCurrentThreadId();

	std::string mapName = oss.str();

	HANDLE fileMap = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa,
		PAGE_READWRITE, 0, MAX_PAGEANT_MESSAGE_SIZE, mapName.c_str());

	if (fileMap == NULL)
	{
		LogDebug(L"cannot create file mapping! ", LogLastError);
		return false;
	}
	auto fmGuard = HandleGuard(fileMap, CloseHandle);

	void* mem = MapViewOfFile(fileMap, FILE_MAP_WRITE, 0, 0, 0);
	if (mem == nullptr)
	{
		LogDebug(L"cannot map memory! ", LogLastError);
		return false;
	}
	auto memGuard = HandleGuard(mem, UnmapViewOfFile);

	char* charMem = reinterpret_cast<char*>(mem);
	uint32_t beLength;

	beLength = htonl(message->length);
	memcpy(charMem, &beLength, HEADER_SIZE);
	memcpy(charMem + HEADER_SIZE, message->data.data(), message->length);

	COPYDATASTRUCT cds;
	cds.dwData = AGENT_COPYDATA_ID;
	cds.cbData = static_cast<int>(mapName.size()) + 1;
	cds.lpData = reinterpret_cast<PVOID>(const_cast<char*>(mapName.c_str()));

	LogDebug(L"send request: length=", message->length, L", type=0x", std::hex,
		std::setfill(L'0'), std::setw(2), message->data[0]);
	// send message
	if (SendMessageW(pageantWindow, WM_COPYDATA, 0,
		reinterpret_cast<LPARAM>(&cds)) > 0)
	{
		LogDebug(L"send request successfully, reading reply.");
		memcpy(&beLength, charMem, HEADER_SIZE);
		message->length = ntohl(beLength);
		message->data.resize(message->length);
		memcpy(message->data.data(), charMem + HEADER_SIZE, message->length);
		LogDebug(L"recv reply: length=", message->length, L", type=0x", std::hex,
			std::setfill(L'0'), std::setw(2), message->data[0]);
		return true;
	}
	LogDebug(L"cannot get reply from Pageant! ", LogLastError);
	return false;
}
