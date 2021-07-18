
#include "log.h"
#include "util.h"
#include "protocol_listener_pageant.h"

#include <cassert>
#include <cstring>

#include <WinSock2.h>

sab::PageantListener::PageantListener()
	: cancelFlag(false), windowHandle(NULL), messageOnAir(false), messageStatus(false)
{
}

bool sab::PageantListener::Run()
{
	WNDCLASSW windowClass;
	ATOM classHandle;
	memset(&windowClass, 0, sizeof(WNDCLASS));
	windowClass.lpszClassName = PAGEANT_CLASS_NAME;
	windowClass.lpfnWndProc = &PageantWindowProcedure;
	windowClass.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
	classHandle = RegisterClassW(&windowClass);
	if (classHandle == 0)
	{
		LogError(L"cannot register window class! ", LogLastError);
		return false;
	}
	auto classGuard = HandleGuard(classHandle, [](auto)
		{
			UnregisterClassW(PAGEANT_CLASS_NAME,
				reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)));
		});

	windowHandle = CreateWindowExW(
		0,
		PAGEANT_CLASS_NAME,
		PAGEANT_WINDOW_NAME,
		0,
		0,
		0,
		0,
		0,
		NULL,
		NULL,
		reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)),
		NULL);
	if (windowHandle == NULL)
	{
		LogError(L"cannot create Pageant window! ", LogLastError);
		return false;
	}

	SetLastError(ERROR_SUCCESS);
	SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	if (GetLastError() != ERROR_SUCCESS)
	{
		LogError(L"cannot set window data! ", LogLastError);
		return false;
	}

	// message loop
	MSG message;
	BOOL status;
	HWND localWindowHandle = windowHandle;
	while ((status = GetMessageW(&message, localWindowHandle, 0, 0)) != 0)
	{
		if (status == -1)
		{
			LogError(L"GetMessage failed! ", LogLastError);
			return false;
		}
		else
		{
			// TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	return true;
}

void sab::PageantListener::Cancel()
{
	{
		std::unique_lock<mutex_type> lg(msgMutex);
		messageOnAir = false;
		messageStatus = false;
		cancelFlag = true;
		msgCondition.notify_one();
	}
	if (windowHandle != NULL) {
		SendMessageW(windowHandle, WM_CLOSE, NULL, NULL);
		windowHandle = NULL;
	}
}

bool sab::PageantListener::IsCancelled() const
{
	return windowHandle == NULL;
}

sab::PageantListener::~PageantListener()
{
}

void sab::PageantListener::SetEmitMessageCallback(std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)>&& callback)
{
	receiveCallback = callback;
}

LRESULT WINAPI sab::PageantListener::PageantWindowProcedure(HWND windowHandle,
	UINT messageId, WPARAM wParam, LPARAM lParam)
{
	COPYDATASTRUCT* cds;
	switch (messageId) {
	case WM_COPYDATA:
		cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
		if (cds->dwData == AGENT_COPYDATA_ID)
		{
			// process request
			LONG_PTR data = GetWindowLongPtrW(windowHandle, GWLP_USERDATA);
			PageantListener* inst = reinterpret_cast<PageantListener*>(data);
			assert(inst != nullptr);
			return inst->ProcessRequest(cds);
		}
		else
		{
			return 0;
		}
	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(windowHandle, messageId, wParam, lParam);
	}
}

int sab::PageantListener::ProcessRequest(COPYDATASTRUCT* cds)
{
	LogDebug(L"processing request...");
	char* mapName = static_cast<char*>(cds->lpData);
	if (mapName[cds->cbData - 1] != '\0')
	{
		return false;
	}

	HANDLE fileMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mapName);
	if (fileMap == NULL)
	{
		LogError(L"cannot open provided file mapping! ", LogLastError);
		return false;
	}
	auto mapGuard = HandleGuard(fileMap, CloseHandle);

	auto mapOwner = GetHandleOwnerSid(fileMap);
	auto acceptedOwner1 = GetCurrentUserSidString();
	auto acceptedOwner2 = GetHandleOwnerSid(GetCurrentProcess());

	LogDebug(L"file mapping owner: ", mapOwner);

	if (!CompareStringSid(mapOwner, acceptedOwner1) &&
		!CompareStringSid(mapOwner, acceptedOwner2))
	{
		LogError(L"unauthorized request!");
		return false;
	}

	void* mem = MapViewOfFile(fileMap, FILE_MAP_WRITE, 0, 0, 0);
	if (mem == nullptr)
	{
		LogError(L"cannot map memory! ", LogLastError);
		return false;
	}
	auto memGuard = HandleGuard(mem, UnmapViewOfFile);
	char* charMem = static_cast<char*>(mem);

	// process actual request
	std::shared_ptr<SshMessageEnvelope> message = std::make_shared<SshMessageEnvelope>();
	std::weak_ptr<SshMessageEnvelope> weakMessage(message);
	uint32_t beLength;
	memcpy(&beLength, charMem, HEADER_SIZE);
	message->replyCallback = [this, weakMessage](SshMessageEnvelope*, bool status)
	{
		if (weakMessage.lock() == nullptr)
			return; // operation canceled
		std::unique_lock<mutex_type> lg(msgMutex);
		messageOnAir = false;
		messageStatus = status;
		msgCondition.notify_one();
	};
	message->length = ntohl(beLength);
	message->data.clear();
	message->data.insert(message->data.begin(), charMem + HEADER_SIZE,
		charMem + HEADER_SIZE + message->length);

	LogDebug(L"recv message: length=", message->length, L", type=0x",
		std::hex, std::setfill(L'0'), std::setw(2), message->data[0]);

	{
		std::unique_lock<mutex_type> lg(msgMutex);
		if (cancelFlag)return false;
		assert(messageOnAir != true);
		messageOnAir = true;
		receiveCallback(message.get(), message);
		msgCondition.wait(lg, [&]()
			{
				return messageOnAir == false;
			});
	}

	if (!messageStatus)
	{
		LogDebug(L"no valid reply received!");
		return false;
	}

	LogDebug(L"send message: length=", message->length, L", type=0x",
		std::hex, std::setfill(L'0'), std::setw(2), message->data[0]);

	if (message->length + HEADER_SIZE > MAX_PAGEANT_MESSAGE_SIZE)
	{
		LogError(L"message too long!");
		return false;
	}

	beLength = htonl(message->length);
	memcpy(charMem, &beLength, HEADER_SIZE);
	memcpy(charMem + HEADER_SIZE, message->data.data(), message->data.size());

	return true;
}
