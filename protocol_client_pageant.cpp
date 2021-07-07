
#include "log.h"
#include "util.h"
#include "protocol_client_pageant.h"

#include <Windows.h>

sab::PageantClient::PageantClient()
{
}

sab::PageantClient::~PageantClient()
{
}

bool sab::PageantClient::SendSshMessage(SshMessageEnvelope* message)
{
	std::ostringstream oss;

	if(message->length+HEADER_SIZE>MAX_PAGEANT_MESSAGE_SIZE)
	{
		LogDebug(L"message too long!");
		return false;
	}
	
	HWND pageantWindow = FindWindowW(L"Pageant", L"Pageant");

	if (pageantWindow == NULL)
	{
		LogDebug(L"cannot find Pageant communication window! ", LogLastError);
		return false;
	}

	oss << "PageantRequest" << std::hex << std::setfill('0') << std::setw(8)
		<< (uint32_t)GetCurrentThreadId();
	
	std::string mapName = oss.str();

	HANDLE fileMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,  // FIXME: SecurityAttributes
		PAGE_READWRITE, 0, MAX_PAGEANT_MESSAGE_SIZE, mapName.c_str());

	if(fileMap==NULL)
	{
		LogDebug(L"cannot create file mapping! ", LogLastError);
		return false;
	}
	auto fmGuard = HandleGuard(fileMap, CloseHandle);

	void* mem = MapViewOfFile(fileMap, FILE_MAP_WRITE, 0, 0, 0);
	if(mem==nullptr)
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
	cds.cbData = mapName.size() + 1;
	cds.lpData = reinterpret_cast<PVOID>(const_cast<char*>(mapName.c_str()));

	// send message
	if(SendMessageW(pageantWindow, WM_COPYDATA, 0, 
		reinterpret_cast<LPARAM>(&cds))>0)
	{
		memcpy(&beLength, charMem, HEADER_SIZE);
		message->length = ntohl(beLength);
		message->data.resize(message->length);
		memcpy(message->data.data(), charMem + HEADER_SIZE, message->length);
		return true;
	}
	LogDebug(L"cannot get reply from Pageant! ", LogLastError);
	return false;
}
