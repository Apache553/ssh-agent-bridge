
#pragma once

#include <atomic>
#include <string>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <combaseapi.h>
#include <WbemIdl.h>
#include <WinSock2.h>

#include <wil/com.h>
#include <wil/resource.h>


namespace sab
{
	class WSL2SocketRebindNotifier
	{	
	private:
		wil::unique_event_nothrow notifyEvent;

		bool started = false;
		std::mutex mutex;

		wil::unique_hmodule errormsgModule;
	private:
		class EventSink :public IWbemObjectSink
		{
		private:
			std::atomic<ULONG> refCount = 1;

			WSL2SocketRebindNotifier* notifier;


		public:
			HRESULT QueryInterface(const IID& riid, void** ppvObject) override;
			ULONG AddRef() override;
			ULONG Release() override;
			HRESULT Indicate(long lObjectCount, IWbemClassObject** apObjArray) override;
			HRESULT SetStatus(long lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject* pObjParam) override;

			EventSink(WSL2SocketRebindNotifier* notifier);
			~EventSink() = default;
		};

		friend class EventSink;
		
		wil::unique_couninitialize_call comUninitGuard;
		
		wil::com_ptr_nothrow<IWbemLocator> locator;
		wil::com_ptr_nothrow<IWbemServices> services;
		wil::com_ptr_nothrow<IUnsecuredApartment> apartment;
		wil::com_ptr_nothrow<EventSink> eventSink;
		wil::com_ptr_nothrow<IWbemObjectSink> eventSinkStub;

		GUID lastVmId;

		std::wstring selfSid;

		GUID TryGetVmIdFromCmdline(const std::wstring& cmdline);
		bool CheckWmiProcessOwner(IWbemClassObject* process);
		GUID UpdateLastVmIdByWmiProcess(IWbemClassObject* process);
		GUID UpdateLastVmid(GUID newVmId);
	public:
		GUID FlushLastWslVmId();
		GUID GetLastWslVmId();
		HANDLE GetEventHandle();

		bool Start();
		void Stop();

		WSL2SocketRebindNotifier();
		~WSL2SocketRebindNotifier();
	};
}
