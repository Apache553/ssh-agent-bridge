
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{
	class ServiceSupport
	{
	public:
		static constexpr wchar_t SERVICE_NAME[] = L"ssh-agent-bridge";
		static constexpr wchar_t SERVICE_DISPLAY_NAME[] = L"OpenSSH Authentication Agent Bridge";
	private:
		SERVICE_STATUS_HANDLE statusHandle;

		SERVICE_STATUS status;
	public:
		ServiceSupport();
		~ServiceSupport();

		bool RegisterService(const wchar_t* name, LPHANDLER_FUNCTION func);
		void ReportStatus(DWORD currentState, DWORD exitCode, DWORD waitHint = 0);

		static bool InstallService();
		static bool UninstallService();

		static void AdjustProcessACL();

		static ServiceSupport& GetInstance();
	private:
	};
}