
#include "log.h"
#include "util.h"
#include "service_support.h"

#include <sddl.h>
#include <AclAPI.h>

sab::ServiceSupport::ServiceSupport()
	:statusHandle(NULL)
{
	memset(&status, 0, sizeof(SERVICE_STATUS));
	status.dwServiceType = SERVICE_USER_OWN_PROCESS;
}

sab::ServiceSupport::~ServiceSupport()
{
}

bool sab::ServiceSupport::RegisterService(const wchar_t* name, LPHANDLER_FUNCTION func)
{
	statusHandle = RegisterServiceCtrlHandlerW(name, func);
	if (statusHandle == NULL)
		return false;
	AdjustProcessACL();
	return true;

}

bool sab::ServiceSupport::InstallService()
{
	SC_HANDLE managerHandle = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (managerHandle == NULL)
	{
		LogError(L"cannot open scm!", LogLastError);
		return false;
	}
	auto managerGuard = HandleGuard(managerHandle, CloseServiceHandle);

	auto cmdline = GetExecutablePath() + L" /Service";

	SC_HANDLE serviceHandle = CreateServiceW(
		managerHandle,
		SERVICE_NAME,
		SERVICE_DISPLAY_NAME,
		SERVICE_ALL_ACCESS,
		SERVICE_USER_OWN_PROCESS,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_NORMAL,
		cmdline.c_str(),
		NULL,
		NULL,
		NULL,
		NULL,
		NULL);
	if (serviceHandle == NULL)
	{
		LogError(L"cannot create service! ", LogLastError);
		return false;
	}
	auto serviceGuard = HandleGuard(serviceHandle, CloseServiceHandle);

	return true;
}

bool sab::ServiceSupport::UninstallService()
{
	SC_HANDLE managerHandle = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
	if (managerHandle == NULL)
	{
		LogError(L"cannot open scm!", LogLastError);
		return false;
	}
	auto managerGuard = HandleGuard(managerHandle, CloseServiceHandle);

	SC_HANDLE serviceHandle = OpenServiceW(managerHandle, SERVICE_NAME, DELETE);
	if (serviceHandle == NULL)
	{
		LogError(L"cannot open service! ", LogLastError);
		return false;
	}
	auto serviceGuard = HandleGuard(serviceHandle, CloseServiceHandle);

	if (DeleteService(serviceHandle) == 0)
	{
		LogError(L"failed to uninstall service! ", LogLastError);
		return false;
	}
	return true;
}

void sab::ServiceSupport::AdjustProcessACL()
{
	DWORD err = ERROR_SUCCESS;
	PSECURITY_DESCRIPTOR psd = nullptr;
	PSECURITY_DESCRIPTOR psd2 = nullptr;
	err = GetSecurityInfo(GetCurrentProcess(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, nullptr, nullptr, &psd);
	if (err != ERROR_SUCCESS)
	{
		SetLastError(err);
		LogError(L"cannot GetSecurityInfo on current process! ", LogLastError);
		return;
	}
	auto sdGuard = HandleGuard(psd, LocalFree);

	wchar_t* str;
	ULONG len;
	if (ConvertSecurityDescriptorToStringSecurityDescriptorW(psd, SDDL_REVISION_1,
		DACL_SECURITY_INFORMATION, &str, &len) == 0)
	{
		LogError(L"cannot convert security descriptor to string! ", LogLastError);
		return;
	}
	auto sddlGuard = HandleGuard(str, LocalFree);

	std::wostringstream oss;
	// allow SYNCHRONIZE PROCESS_QUERY_INFORMATION PROCESS_DUP_HANDLE for local system account
	oss << str << L"(A;;0x100440;;;SY)";

	if (ConvertStringSecurityDescriptorToSecurityDescriptorW(oss.str().c_str(),
		SDDL_REVISION_1, &psd2, &len) == 0)
	{
		LogError(L"cannot convert string to security descriptor! ", LogLastError);
		return;
	}
	auto sdGuard2 = HandleGuard(psd2, LocalFree);

	BOOL daclPresentFlag;
	BOOL defaultedDaclFlag;
	PACL dacl;
	if (GetSecurityDescriptorDacl(psd2, &daclPresentFlag, &dacl, &defaultedDaclFlag) == 0)
	{
		LogError(L"cannot get dacl from security descriptor! ", LogLastError);
		return;
	}
	if (daclPresentFlag == FALSE)
	{
		LogError(L"dacl is not present in security descriptor!");
		return;
	}
	err = SetSecurityInfo(GetCurrentProcess(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, dacl, nullptr);
	if (err != ERROR_SUCCESS)
	{
		SetLastError(err);
		LogError(L"cannot SetSecurityInfo on current process! ", LogLastError);
		return;
	}
}

sab::ServiceSupport& sab::ServiceSupport::GetInstance()
{
	static ServiceSupport inst;
	return inst;
}

void sab::ServiceSupport::ReportStatus(DWORD currentState, DWORD exitCode, DWORD waitHint)
{
	if (statusHandle == NULL)
		return;
	if ((currentState == SERVICE_RUNNING) ||
		(currentState == SERVICE_STOPPED))
	{
		status.dwCheckPoint = 0;
	}
	else
	{
		++status.dwCheckPoint;
	}
	if (currentState == SERVICE_RUNNING)
	{
		status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	}
	else
	{
		status.dwControlsAccepted = 0;
	}
	status.dwCurrentState = currentState;
	status.dwWin32ExitCode = exitCode;
	status.dwWaitHint = waitHint;

	BOOL r = SetServiceStatus(statusHandle, &status);

}
