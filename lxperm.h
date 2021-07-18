
#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{

	struct LxPermissionInfo
	{
		int uid = 1000;
		int gid = 1000;
		int mode = 0777;
	};

	bool SetLxPermission(const std::wstring& path, const LxPermissionInfo& perm, bool isSpecial);
	bool SetLxPermissionByHandle(HANDLE fileHandle, const LxPermissionInfo& perm);
}