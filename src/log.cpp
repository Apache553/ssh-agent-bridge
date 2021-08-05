#include "log.h"
#include "util.h"

#include <ctime>
#include <iomanip>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <codecvt>
#include <Windows.h>
#include <io.h>
#include <fcntl.h>

static const wchar_t* TranslateLogLevel(sab::Logger::LogLevel level)noexcept
{
	switch (level)
	{
	case sab::Logger::LogLevel::Debug:
		return L"Debug";
	case sab::Logger::LogLevel::Info:
		return L"Info";
	case sab::Logger::LogLevel::Warning:
		return L"Warning";
	case sab::Logger::LogLevel::Error:
		return L"Error";
	}
	return L""; // shut compiler up
}

static FILE* OpenHandleToFILE(HANDLE handle, const wchar_t* mode)noexcept
{
	HANDLE handle2;

	if (handle == NULL || handle == INVALID_HANDLE_VALUE)
		return nullptr;
	/*
	 * _open_osfhandle and fdopen will take over the ownership of a handle,
	 * duplicate one to avoid double close a handle exception
	 */
	DuplicateHandle(
		GetCurrentProcess(),
		handle,
		GetCurrentProcess(),
		&handle2,
		0,
		FALSE,
		DUPLICATE_SAME_ACCESS);
	if (handle2 == NULL || handle2 == INVALID_HANDLE_VALUE)
		return nullptr;
	auto handle2Guard = sab::HandleGuard(handle2, CloseHandle);

	int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle2), _O_TEXT);
	if (fd == -1)
		return nullptr;
	auto fdGuard = sab::HandleGuard(fd, _close);

	FILE* file = _wfdopen(fd, mode);
	if (file == nullptr)
		return nullptr;

	std::setvbuf(file, nullptr, _IONBF, 0);

	handle2Guard.release();
	fdGuard.release();

	return file;
}


bool sab::Logger::PrepareConsole()
{
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
	{
		if (GetLastError() != ERROR_ACCESS_DENIED && !AllocConsole())
		{
			// failed to get a console
			return false;
		}
	}
	// set console codepage to UTF8 for better unicode support
	SetConsoleOutputCP(CP_UTF8);

	stdinStream = OpenHandleToFILE(GetStdHandle(STD_INPUT_HANDLE), L"r");
	stdoutStream = OpenHandleToFILE(GetStdHandle(STD_OUTPUT_HANDLE), L"w");
	stderrStream = OpenHandleToFILE(GetStdHandle(STD_ERROR_HANDLE), L"w");

	if (!(stdinStream && stdoutStream && stderrStream))
	{
		if (stdinStream)fclose(stdinStream);
		if (stdoutStream)fclose(stdoutStream);
		if (stderrStream)fclose(stderrStream);
		FreeConsole();
		return false;
	}

	allocatedConsole = true;
	return true;
}

void sab::Logger::FreeConsole()
{
	if (allocatedConsole)
	{
		std::fclose(stdinStream);
		std::fclose(stdoutStream);
		std::fclose(stderrStream);
		::FreeConsole();
	}
}

bool sab::Logger::PrepareFileLog()
{
	auto path = ReplaceEnvironmentVariables(L"%APPDATA%\\ssh-agent-bridge.log");
	fileStream.imbue(std::locale(std::locale::empty(), new std::codecvt_utf8<wchar_t>));
	fileStream.open(path, std::ios::app);
	if (!fileStream.is_open())
		return false;
	fileStream.rdbuf()->pubsetbuf(nullptr, 0);
	fileStream << L"=== NEW LOG SESSION ===\n";
	return true;
}

void sab::Logger::FreeFileLog()
{
	return fileStream.close();
}


void sab::Logger::WriteLogImpl(LogLevel level, const wchar_t* file, int line,
	const std::wstring& str)noexcept
{
	std::time_t curTime = std::time(nullptr);
	std::tm tm;
	localtime_s(&tm, &curTime);
	std::wostringstream oss;
	oss << L'[' << std::put_time(&tm, L"%F %T");
	oss << L"][" << std::this_thread::get_id();
	oss << L"][" << TranslateLogLevel(level) << L"] ";
	oss << file << L':' << line;
	oss << L": " << str << L'\n';
	auto logStr = oss.str();
	std::lock_guard<std::mutex> lg(ioMutex);
	if (allocatedConsole)
	{
		std::fwprintf(stdoutStream, L"%s", logStr.c_str());
	}
	if (debugOutput) {
		OutputDebugStringW(logStr.c_str());
	}
	if (fileStream.is_open())
	{
		fileStream << logStr;
	}
}

void sab::Logger::SetLogOutputLevel(LogLevel level)
{
	outputLevel = level;
	LogInfo(L"set log level: ", TranslateLogLevel(level));
}

sab::Logger& sab::Logger::GetInstance(bool isDebug, bool allocConsole)noexcept
{
	static Logger instance(isDebug, allocConsole);
	return instance;
}

sab::Logger::Logger(bool isDebug, bool allocConsole)noexcept
	:stdinStream(nullptr), stdoutStream(nullptr), stderrStream(nullptr),
	outputLevel(LogLevel::Info)
{
	std::setlocale(LC_ALL, ".utf8");

	debugOutput = isDebug;
	if (allocConsole)
	{
		allocatedConsole = PrepareConsole();
	}
	PrepareFileLog();
}

sab::Logger::~Logger()noexcept
{
	FreeConsole();
	FreeFileLog();
}
