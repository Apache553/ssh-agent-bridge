#include "log.h"

#include <ctime>
#include <iomanip>

#include <Windows.h>
#include <io.h>
#include <fcntl.h>

static const wchar_t* TranslateLogLevel(sab::Logger::LogLevel level)noexcept
{
	switch (level)
	{
	case sab::Logger::Debug:
		return L"Debug";
	case sab::Logger::Info:
		return L"Info";
	case sab::Logger::Warning:
		return L"Warning";
	case sab::Logger::Error:
		return L"Error";
	case sab::Logger::Fatal:
		return L"FATAL";
	}
	return L""; // shut compiler up
}

void sab::Logger::WriteLogImpl(LogLevel level, const wchar_t* file, int line,
	const std::wstring& str)noexcept
{
	std::time_t curTime = std::time(nullptr);
	std::tm tm;
	localtime_s(&tm, &curTime);
	std::wostringstream oss;
	oss << L'[' << std::put_time(&tm, L"%F %T");
	oss << L"][" << TranslateLogLevel(level) << L"] ";
	oss << file << L':' << line;
	std::fwprintf(stdoutStream, L"%s: %s\n", oss.str().c_str(), str.c_str());
}

sab::Logger& sab::Logger::GetInstance(bool createConsole)noexcept
{
	static Logger instance(createConsole);
	return instance;
}

FILE* OpenHandleToFILE(HANDLE handle, const wchar_t* mode)noexcept
{
	HANDLE handle2;


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
	
	
	int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle2), _O_TEXT);
	FILE* file = _wfdopen(fd, mode);
	std::setvbuf(file, nullptr, _IONBF, 0);
	return file;
}

sab::Logger::Logger(bool createConsole)noexcept
	:stdinStream(nullptr), stdoutStream(nullptr), stderrStream(nullptr),
	allocatedConsole(createConsole)
{
	std::setlocale(LC_ALL, "");

	if (createConsole)
	{
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
		{
			if (GetLastError() != ERROR_ACCESS_DENIED && !AllocConsole())
			{
				// failed to get a console
				exit(255);
			}
		}
		// set console codepage to UTF8 for better unicode support
		SetConsoleOutputCP(CP_UTF8);
		std::setlocale(LC_ALL, ".utf8");

		stdinStream = OpenHandleToFILE(GetStdHandle(STD_INPUT_HANDLE), L"r");
		stdoutStream = OpenHandleToFILE(GetStdHandle(STD_OUTPUT_HANDLE), L"w");
		stderrStream = OpenHandleToFILE(GetStdHandle(STD_ERROR_HANDLE), L"w");
	}
	else
	{
		// leave streams null
		allocatedConsole = 0;
	}
}

sab::Logger::~Logger()noexcept
{
	if (allocatedConsole)
	{
		std::fclose(stdinStream);
		std::fclose(stdoutStream);
		std::fclose(stderrStream);
		FreeConsole();
	}
}
