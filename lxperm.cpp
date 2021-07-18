
#include "lxperm.h"
#include "log.h"
#include "util.h"

#include <vector>
#include <memory>
#include <tuple>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winternl.h>
#include <sys/stat.h>

// declare structs and functions to manipulate Lx Attributes
constexpr int FileStatInformation = 68;
constexpr int FileStatLxInformation = 70;

constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_NO_EAS_ON_FILE = 0xC0000052;

constexpr ULONG LX_FILE_METADATA_HAS_UID = 0x1;
constexpr ULONG LX_FILE_METADATA_HAS_GID = 0x2;
constexpr ULONG LX_FILE_METADATA_HAS_MODE = 0x4;

#pragma pack(push, 1)
typedef struct _FILE_FULL_EA_INFORMATION {
	ULONG                   NextEntryOffset;
	BYTE                    Flags;
	BYTE                    EaNameLength;
	USHORT                  EaValueLength;
	CHAR                    EaName[1];
} FILE_FULL_EA_INFORMATION, * PFILE_FULL_EA_INFORMATION;


typedef struct _FILE_STAT_LX_INFORMATION {
	LARGE_INTEGER FileId;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	ULONG         FileAttributes;
	ULONG         ReparseTag;
	ULONG         NumberOfLinks;
	ACCESS_MASK   EffectiveAccess;
	ULONG         LxFlags;
	ULONG         LxUid;
	ULONG         LxGid;
	ULONG         LxMode;
	ULONG         LxDeviceIdMajor;
	ULONG         LxDeviceIdMinor;
} FILE_STAT_LX_INFORMATION, * PFILE_STAT_LX_INFORMATION;

typedef struct _FILE_STAT_INFORMATION {
	LARGE_INTEGER FileId;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	ULONG         FileAttributes;
	ULONG         ReparseTag;
	ULONG         NumberOfLinks;
	ACCESS_MASK   EffectiveAccess;
} FILE_STAT_INFORMATION, * PFILE_STAT_INFORMATION;

#pragma pack(pop)

typedef
NTSTATUS
(NTAPI* NtSetInformationFileFn)(
	IN HANDLE               FileHandle,
	OUT PIO_STATUS_BLOCK    IoStatusBlock,
	IN PVOID                FileInformation,
	IN ULONG                Length,
	IN FILE_INFORMATION_CLASS FileInformationClass);

typedef
NTSTATUS
(NTAPI* NtQueryInformationFileFn)(
	IN HANDLE               FileHandle,
	OUT PIO_STATUS_BLOCK    IoStatusBlock,
	OUT PVOID               FileInformation,
	IN ULONG                Length,
	IN FILE_INFORMATION_CLASS FileInformationClass);

typedef
NTSTATUS
(NTAPI* NtQueryEaFileFn)(
	IN HANDLE               FileHandle,
	OUT PIO_STATUS_BLOCK    IoStatusBlock,
	OUT PVOID               Buffer,
	IN ULONG                Length,
	IN BOOLEAN              ReturnSingleEntry,
	IN PVOID                EaList OPTIONAL,
	IN ULONG                EaListLength,
	IN PULONG               EaIndex OPTIONAL,
	IN BOOLEAN              RestartScan);

typedef
NTSTATUS
(NTAPI* NtSetEaFileFn)(
	IN HANDLE               FileHandle,
	OUT PIO_STATUS_BLOCK    IoStatusBlock,
	IN PVOID                EaBuffer,
	IN ULONG                EaBufferSize);



class EaInfoBuilder
{
public:
	static constexpr size_t ALIGN = alignof(LONG);
private:
	std::vector<char> buffer;
	ptrdiff_t last = -1;
public:
	void EmitEa(BYTE flags, const std::string& name, const std::string& value)
	{
		if (last != -1)
		{
			FILE_FULL_EA_INFORMATION& info = *reinterpret_cast<PFILE_FULL_EA_INFORMATION>(buffer.data() + last);
			info.NextEntryOffset = static_cast<ULONG>(buffer.size() - last);
		}
		last = buffer.size();
		buffer.resize(buffer.size() + sizeof(FILE_FULL_EA_INFORMATION) - 1);
		FILE_FULL_EA_INFORMATION& info = *reinterpret_cast<PFILE_FULL_EA_INFORMATION>(buffer.data() + last);
		info.Flags = flags;
		info.EaNameLength = static_cast<BYTE>(name.size());
		info.EaValueLength = static_cast<USHORT>(value.size());
		for (auto ch : name)
			buffer.push_back(ch);
		buffer.push_back('\0');
		for (auto ch : value)
			buffer.push_back(ch);
		while (buffer.size() % ALIGN)
			buffer.push_back(0);
	}
	std::tuple<std::unique_ptr<char[]>, void*, size_t> FinishEaList()
	{
		std::tuple<std::unique_ptr<char[]>, void*, size_t> ret;
		if (last != -1)
		{
			FILE_FULL_EA_INFORMATION& info = *reinterpret_cast<PFILE_FULL_EA_INFORMATION>(buffer.data() + last);
			info.NextEntryOffset = 0;
		}
		std::get<0>(ret) = std::unique_ptr<char[]>(new char[buffer.size() + ALIGN]);
		char* aligned = std::get<0>(ret).get();
		while (reinterpret_cast<size_t>(aligned) % ALIGN)
			++aligned;
		memcpy(aligned, buffer.data(), buffer.size());
		std::get<1>(ret) = aligned;
		std::get<2>(ret) = buffer.size();
		buffer.resize(0);
		buffer.shrink_to_fit();
		return ret;
	}
};


template<typename T>
static std::string MakeByteString(T data)
{
	std::string ret;
	for (size_t i = 0; i < sizeof(T); ++i)
	{
		ret.push_back(*(reinterpret_cast<char*>(&data) + i));
	}
	return ret;
}

bool sab::SetLxPermission(const std::wstring& path, const LxPermissionInfo& perm, bool isSpecial)
{
	HANDLE fileHandle = CreateFileW(path.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | (isSpecial ? FILE_FLAG_OPEN_REPARSE_POINT : NULL),
		NULL);
	auto socketFileGuard = HandleGuard(fileHandle, CloseHandle);
	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		LogError(L"cannot open file! ", LogLastError);
		socketFileGuard.release();
		return false;
	}

	return SetLxPermissionByHandle(fileHandle, perm);
}

bool sab::SetLxPermissionByHandle(HANDLE fileHandle, const LxPermissionInfo& perm)
{
	NtQueryInformationFileFn NtQueryInformationFile = nullptr;
	NtSetInformationFileFn NtSetInformationFile = nullptr;
	NtQueryEaFileFn NtQueryEaFile = nullptr;
	NtSetEaFileFn NtSetEaFile = nullptr;
	HMODULE ntdllHandle = GetModuleHandleW(L"ntdll.dll");
	if (ntdllHandle == NULL)
	{
		LogError(L"cannot get handle of ntdll.dll! ", LogLastError);
		return false;
	}
	NtQueryInformationFile = reinterpret_cast<NtQueryInformationFileFn>(GetProcAddress(ntdllHandle, "NtQueryInformationFile"));
	if (NtQueryInformationFile == nullptr)
	{
		LogError(L"cannot get NtQueryInformationFile's pointer! ", LogLastError);
		return false;
	}
	NtSetInformationFile = reinterpret_cast<NtSetInformationFileFn>(GetProcAddress(ntdllHandle, "NtSetInformationFile"));
	if (NtSetInformationFile == nullptr)
	{
		LogError(L"cannot get NtSetInformationFile's pointer! ", LogLastError);
		return false;
	}
	NtQueryEaFile = reinterpret_cast<NtQueryEaFileFn>(GetProcAddress(ntdllHandle, "NtQueryEaFile"));
	if (NtQueryEaFile == nullptr)
	{
		LogError(L"cannot get NtQueryEaFile's pointer! ", LogLastError);
		return false;
	}
	NtSetEaFile = reinterpret_cast<NtSetEaFileFn>(GetProcAddress(ntdllHandle, "NtSetEaFile"));
	if (NtSetEaFile == nullptr)
	{
		LogError(L"cannot get NtSetEaFile's pointer! ", LogLastError);
		return false;
	}

	BY_HANDLE_FILE_INFORMATION fileInfo;
	if (GetFileInformationByHandle(fileHandle, &fileInfo) == 0)
	{
		LogError(L"cannot get file info! ", LogLastError);
		return false;
	}

	IO_STATUS_BLOCK ioStatus;
	NTSTATUS r;

	//FILE_STAT_LX_INFORMATION info;
	//r = NtQueryInformationFile(socketFileHandle, &ioStatus, &info, sizeof(info),
	//	static_cast<FILE_INFORMATION_CLASS>(FileStatLxInformation));
	//if (r != STATUS_SUCCESS)
	//{
	//	LogError(L"NtQueryInformationFile failed! ", LogNtStatus(r));
	//	return false;
	//}

	int realMode = perm.mode & 0777;
	if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		realMode |= S_IFDIR;
	else
		realMode |= S_IFREG;

	EaInfoBuilder builder;
	if (perm.uid >= 0)builder.EmitEa(0, "$LXUID", MakeByteString<ULONG>(perm.uid));
	if (perm.gid >= 0)builder.EmitEa(0, "$LXGID", MakeByteString<ULONG>(perm.gid));
	if (perm.mode >= 0)builder.EmitEa(0, "$LXMOD", MakeByteString<ULONG>(realMode));
	auto rr = builder.FinishEaList();

	r = NtSetEaFile(fileHandle, &ioStatus, std::get<1>(rr),
		static_cast<ULONG>(std::get<2>(rr)));
	if (r != STATUS_SUCCESS)
	{
		LogError(L"NtSetEaFile failed! ", LogNtStatus(r));
		return false;
	}

	//r = NtQueryInformationFile(socketFileHandle, &ioStatus, &info, sizeof(info),
	//	static_cast<FILE_INFORMATION_CLASS>(FileStatLxInformation));
	//if (r != STATUS_SUCCESS)
	//{
	//	LogError(L"NtQueryInformationFile failed! ", LogNtStatus(r));
	//	return false;
	//}
	return true;
}
