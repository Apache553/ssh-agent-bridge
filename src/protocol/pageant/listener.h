
#pragma once

#include "../listener_base.h"

#include <condition_variable>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{
	class PageantListener :public ProtocolListenerBase
	{
	public:
		static constexpr size_t MAX_PAGEANT_MESSAGE_SIZE = 8192;
		static constexpr unsigned int AGENT_COPYDATA_ID = 0x804e50ba;
		static constexpr wchar_t PAGEANT_CLASS_NAME[] = L"Pageant";
		static constexpr wchar_t PAGEANT_WINDOW_NAME[] = L"Pageant";

		// use recursive_mutex for single-threaded testing
		using mutex_type = std::mutex;
		// use condition_variable_any for single-threaded testing
		using condition_variable_type = std::condition_variable;
	private:
		bool cancelFlag;

		HWND windowHandle;

		bool messageOnAir;
		bool messageStatus;

		mutex_type msgMutex;
		condition_variable_type msgCondition;

		std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)> receiveCallback;

		bool permissionCheckFlag;
		bool allowNonElevatedAccessFlag;
	public:
		PageantListener(bool permissionCheckFlag, bool allowNonElevatedAccessFlag);

		bool Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		~PageantListener()override;

		void SetEmitMessageCallback(std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)>&& callback);
	private:
		static LRESULT WINAPI PageantWindowProcedure(HWND, UINT, WPARAM, LPARAM);

		int ProcessRequest(COPYDATASTRUCT* cds);
	};
}