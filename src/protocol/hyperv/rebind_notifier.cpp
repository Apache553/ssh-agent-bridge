
#include <cassert>
#include <cstdio>
#include <sstream>

#include "rebind_notifier.h"
#include "../../log.h"
#include "../../util.h"


HRESULT sab::WSL2SocketRebindNotifier::EventSink::QueryInterface(const IID& riid, void** ppvObject)
{
	if (riid == IID_IUnknown)
	{
		*ppvObject = static_cast<IUnknown*>(this);
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IWbemObjectSink)
	{
		*ppvObject = static_cast<IWbemObjectSink*>(this);
		AddRef();
		return S_OK;
	}
	else
	{
		return E_NOINTERFACE;
	}
}

ULONG sab::WSL2SocketRebindNotifier::EventSink::AddRef()
{
	return ++refCount;
}

ULONG sab::WSL2SocketRebindNotifier::EventSink::Release()
{
	ULONG result = --refCount;
	if (result == 0)
		delete this;
	return result;
}

HRESULT sab::WSL2SocketRebindNotifier::EventSink::Indicate(long lObjectCount, IWbemClassObject** apObjArray)
{
	HRESULT hr = S_OK;
	for (int i = 0; i < lObjectCount; i++)
	{
		IWbemClassObject* obj = apObjArray[i];
		wil::unique_variant instanceUnk;
		hr = obj->Get(L"TargetInstance", 0, &instanceUnk, nullptr, nullptr);
		if (FAILED(hr))
		{
			continue;
		}
		wil::com_ptr_nothrow<IWbemClassObject> process;
		hr = instanceUnk.punkVal->QueryInterface(IID_IWbemClassObject, process.put_void());
		if (FAILED(hr))
		{
			continue;
		}
		notifier->UpdateLastVmIdByWmiProcess(process.get());
	}

	return WBEM_S_NO_ERROR;
}

HRESULT sab::WSL2SocketRebindNotifier::EventSink::SetStatus(long lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject* pObjParam)
{
	return WBEM_S_NO_ERROR;
}

sab::WSL2SocketRebindNotifier::EventSink::EventSink(WSL2SocketRebindNotifier* notifier)
	:notifier(notifier)
{
}

GUID sab::WSL2SocketRebindNotifier::TryGetVmIdFromCmdline(const std::wstring& cmdline)
{
	GUID guid = GUID_NULL;
	std::wistringstream iss(cmdline);
	std::wstring arg;
	int count = 0;

	LogDebug(L"cmdline: ", cmdline);

	while (!iss.eof())
	{
		GUID guid_temp;
		iss >> arg;
		if (arg.size() != 38)continue; // 38 is the length of a braced guid
		HRESULT hr = IIDFromString(arg.c_str(), &guid_temp);
		if (SUCCEEDED(hr))
		{
			guid = guid_temp;
			++count;
		}
	}

	// only wslhost.exe whose commandline contains two guids is a WSL2 instance
	// the latter one is vmid
	if (count > 1)
		return guid;
	else
		return GUID_NULL;
}

bool sab::WSL2SocketRebindNotifier::CheckWmiProcessOwner(IWbemClassObject* process)
{
	HRESULT hr;
	wil::unique_variant objectPath;

	hr = process->Get(L"__PATH", 0, &objectPath, nullptr, nullptr);
	if (FAILED(hr))
	{
		LogError(L"cannot get wbem object path! ", LogHRESULTInModule(hr, errormsgModule.get()));
		return false;
	}
	LogDebug(L"get wbem object ", objectPath.bstrVal);

	wil::com_ptr_nothrow<IWbemClassObject> methodOut;
	hr = services->ExecMethod(
		objectPath.bstrVal,
		wil::make_bstr_nothrow(L"GetOwnerSid").get(),
		0,
		nullptr,
		nullptr,
		methodOut.put(),
		nullptr);
	if (FAILED(hr))
	{
		LogError(L"cannot execute GetOwnerSid method! ", LogHRESULTInModule(hr, errormsgModule.get()));
		return false;
	}

	wil::unique_variant sid;
	wil::unique_variant returnValue;
	hr = methodOut->Get(L"ReturnValue", 0, &returnValue, nullptr, nullptr);
	if (FAILED(hr))
	{
		LogError(L"cannot get GetOwnerSid return value! ", LogHRESULTInModule(hr, errormsgModule.get()));
		return false;
	}
	LogDebug(L"method GetOwnerSid returned ", returnValue.uintVal);
	if (returnValue.uintVal != 0)
	{
		// error occurred
		return false;
	}

	hr = methodOut->Get(L"Sid", 0, &sid, nullptr, nullptr);
	if (FAILED(hr))
	{
		LogError(L"cannot get GetOwnerSid returned Sid! ", LogHRESULTInModule(hr, errormsgModule.get()));
		return false;
	}
	LogDebug(L"method GetOwnerSid returned Sid ", sid.bstrVal);
	return CompareStringSid(selfSid, sid.bstrVal);
}

GUID sab::WSL2SocketRebindNotifier::UpdateLastVmIdByWmiProcess(IWbemClassObject* process)
{
	HRESULT hr;

	if (!CheckWmiProcessOwner(process))
	{
		return GUID_NULL;
	}

	wil::unique_variant cmdline;
	hr = process->Get(L"CommandLine", 0, &cmdline, nullptr, nullptr);
	if (FAILED(hr))
	{
		LogError(L"cannot get Win32_Process.CommandLine! ", LogHRESULTInModule(hr, errormsgModule.get()));
		return GUID_NULL;
	}

	GUID vmid = TryGetVmIdFromCmdline(cmdline.bstrVal);
	if (vmid != GUID_NULL)
	{
		UpdateLastVmid(vmid);
	}
	return vmid;
}

GUID sab::WSL2SocketRebindNotifier::UpdateLastVmid(GUID newVmId)
{
	LogDebug(L"new vmid ", LogGUID(newVmId));
	std::lock_guard<std::mutex> lg(mutex);
	lastVmId = newVmId;
	notifyEvent.SetEvent();
	return lastVmId;
}

GUID sab::WSL2SocketRebindNotifier::FlushLastWslVmId()
{
	HRESULT hr;
	wil::com_ptr_nothrow<IEnumWbemClassObject> enumerator;

	hr = services->ExecQuery(
		wil::make_bstr_nothrow(L"WQL").get(),
		wil::make_bstr_nothrow(L"SELECT * FROM Win32_Process WHERE Name = 'wslhost.exe'").get(),
		WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
		nullptr,
		enumerator.put());
	if (FAILED(hr))
	{
		LogError(L"services->ExecQuery() failed! ", LogHRESULTInModule(hr, errormsgModule.get()));
		return GUID_NULL;
	}

	ULONG returned = 0;
	while (true)
	{
		wil::com_ptr_nothrow<IWbemClassObject> object;
		enumerator->Next(INFINITE, 1, object.put(), &returned);

		if (returned == 0)
			break;

		UpdateLastVmIdByWmiProcess(object.get());
	}


	return GetLastWslVmId();
}

GUID sab::WSL2SocketRebindNotifier::GetLastWslVmId()
{
	std::lock_guard<std::mutex> lg(mutex);
	return lastVmId;
}

HANDLE sab::WSL2SocketRebindNotifier::GetEventHandle()
{
	return notifyEvent.get();
}

bool sab::WSL2SocketRebindNotifier::Start()
{
	std::lock_guard<std::mutex> lg(mutex);

	if (started)
		return false;
	started = true;

	constexpr size_t WQL_BUFFER_SIZE = 256;
	float queryInterval = 1.0f;
	const wchar_t* wqlTemplate =
		L"SELECT * FROM __InstanceCreationEvent WITHIN %.3f "
		L"WHERE TargetInstance ISA 'Win32_Process' AND TargetInstance.Name = 'wslhost.exe'";
	wchar_t buffer[WQL_BUFFER_SIZE];
	std::swprintf(buffer, WQL_BUFFER_SIZE, wqlTemplate, queryInterval);

	HRESULT hr;
	hr = services->ExecNotificationQueryAsync(
		wil::unique_bstr(SysAllocString(L"WQL")).get(),
		wil::unique_bstr(SysAllocString(buffer)).get(),
		WBEM_FLAG_DONT_SEND_STATUS,
		NULL,
		eventSinkStub.get());
	if (FAILED(hr))
	{
		LogError(L"services->ExecNotificationQueryAsync() failed! ", LogHRESULTInModule(hr, errormsgModule.get()));
		started = false;
		return false;
	}
	LogDebug(L"started wsl2 vmid notifier");
	return true;
}

void sab::WSL2SocketRebindNotifier::Stop()
{
	std::lock_guard<std::mutex> lg(mutex);
	if (started)
	{
		services->CancelAsyncCall(eventSinkStub.get());
	}
	LogDebug(L"stopped wsl2 vmid notifier");
}

sab::WSL2SocketRebindNotifier::WSL2SocketRebindNotifier()
	:lastVmId(GUID_NULL)
{
	HRESULT hr;

	errormsgModule.reset(LoadLibraryW(L"C:\\Windows\\System32\\wbem\\wmiutils.dll"));

	selfSid = GetCurrentUserSidString();
	if (selfSid.empty())
	{
		LogError(L"cannot get self SID!");
		throw std::runtime_error("cannot get self SID");
	}
	// Initialize Event
	hr = notifyEvent.create(wil::EventOptions::ManualReset);
	if (notifyEvent.get() == NULL)
	{
		LogError(L"CreateEvent() failed! ", LogHRESULT(hr));
		throw std::runtime_error("CreateEvent() failed!");
	}

	// Initialize COM
	hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		comUninitGuard.release();
		LogError(L"CoInitializeEx() failed! ", LogHRESULT(hr));
		throw std::runtime_error("CoInitializeEx() failed!");
	}

	hr = CoCreateInstance(
		CLSID_WbemLocator,
		0,
		CLSCTX_INPROC_SERVER,
		IID_IWbemLocator, locator.put_void());
	if (FAILED(hr))
	{
		LogError(L"CoCreateInstance(CLSID_WbemLocator) failed! ", LogHRESULT(hr));
		throw std::runtime_error("CoCreateInstance(CLSID_WbemLocator) failed!");
	}

	wil::unique_bstr serverPath(SysAllocString(L"ROOT\\CIMV2"));

	hr = locator->ConnectServer(
		serverPath.get(),
		NULL,
		NULL,
		0,
		NULL,
		0,
		0,
		services.put());
	if (FAILED(hr)) {
		LogError(L"locator->ConnectServer() failed! ", LogHRESULTInModule(hr, errormsgModule.get()));
		throw std::runtime_error("locator->ConnectServer() failed!");
	}

	hr = CoSetProxyBlanket(
		services.get(),
		RPC_C_AUTHN_WINNT,
		RPC_C_AUTHZ_NONE,
		NULL,
		RPC_C_AUTHN_LEVEL_CALL,
		RPC_C_IMP_LEVEL_IMPERSONATE,
		NULL,
		EOAC_NONE);
	if (FAILED(hr))
	{
		LogError(L"CoSetProxyBlanket(services) failed! ", LogHRESULT(hr));
		throw std::runtime_error("CoSetProxyBlanket(services) failed!");
	}

	hr = CoCreateInstance(
		CLSID_UnsecuredApartment,
		NULL,
		CLSCTX_LOCAL_SERVER,
		IID_IUnsecuredApartment,
		apartment.put_void());
	if (FAILED(hr)) {
		LogError(L"CoCreateInstance(CLSID_UnsecuredApartment) failed! ", LogHRESULT(hr));
		throw std::runtime_error("CoCreateInstance(CLSID_UnsecuredApartment) failed!");
	}

	eventSink = new EventSink(this);

	wil::com_ptr_nothrow<IUnknown> eventSinkStubUnk;

	hr = apartment->CreateObjectStub(eventSink.get(), eventSinkStubUnk.put_unknown());
	if (FAILED(hr))
	{
		LogError(L"apartment->CreateObjectStub() failed! ", LogHRESULTInModule(hr, errormsgModule.get()));
		throw std::runtime_error("apartment->CreateObjectStub() failed!");
	}

	hr = eventSinkStubUnk.query_to(eventSinkStub.put());
	if (FAILED(hr))
	{
		LogError(L"eventSinkStubUnk->QueryInterface() failed! ", LogHRESULT(hr));
		throw std::runtime_error("eventSinkStubUnk->QueryInterface() failed!");
	}
}

sab::WSL2SocketRebindNotifier::~WSL2SocketRebindNotifier()
{
	Stop();
}
