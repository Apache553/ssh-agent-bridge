
#pragma once

#include <cstdio>
#include <sstream>
#include <mutex>

namespace sab
{

	class Logger
	{
	public:
		enum class LogLevel
		{
			Debug = 0,
			Info,
			Warning,
			Error
		};
	private:
		FILE* stdinStream;
		FILE* stdoutStream;
		FILE* stderrStream;

		std::mutex ioMutex;

		int allocatedConsole;

		LogLevel outputLevel;

		void WriteLogImpl(LogLevel level, const wchar_t* file, int line,
			const std::wstring& str)noexcept;

		Logger(bool createConsole)noexcept;
		~Logger()noexcept;
	public:
		template<typename ...Args>
		void WriteLog(LogLevel level, const wchar_t* file, int line,
			Args... args)noexcept
		{
			if (level >= outputLevel) {
				std::wostringstream oss;
				int helper[] = { 0,(oss << args,0)... };
				WriteLogImpl(level, file, line, oss.str());
			}
		}

		void SetLogOutputLevel(LogLevel level);

		static Logger& GetInstance(bool createConsole = false)noexcept;
	};
}

#define WSTR_(x) L ## x
#define WSTR(x) WSTR_(x)

#define LogDebug(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::LogLevel::Debug , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )

#define LogInfo(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::LogLevel::Info , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )

#define LogWarning(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::LogLevel::Warning , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )

#define LogError(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::LogLevel::Error , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )
