
#pragma once

#include <cstdio>
#include <sstream>
#include <cassert>

namespace sab
{

	class Logger
	{
	public:
		enum LogLevel
		{
			Debug = 0,
			Info,
			Warning,
			Error,
			Fatal
		};
	private:
		FILE* stdinStream;
		FILE* stdoutStream;
		FILE* stderrStream;

		void WriteLogImpl(LogLevel level, const wchar_t* file, int line,
			const std::wstring& str)noexcept;

		static inline const wchar_t* TranslateLogLevel(LogLevel level)noexcept
		{
			switch (level)
			{
			case Debug:
				return L"Debug";
			case Info:
				return L"Info";
			case Warning:
				return L"Warning";
			case Error:
				return L"Error";
			case Fatal:
				return L"FATAL";
			}
			return L""; // shut compiler up
		}

		Logger(bool createConsole);
	public:

		template<typename T>
		auto& InsertStream(std::wostringstream& oss, T&& arg)
		{
			oss << arg;
			return oss;
		}
		
		template<typename ...Args>
		void WriteLog(LogLevel level, const wchar_t* file, int line,
			Args... args)noexcept
		{
			std::wostringstream oss;
			int helper[] = { 0,(oss << args,0)... };
			WriteLogImpl(level, file, line, oss.str());
		}

		static Logger& GetInstance(bool createConsole = false);
	};
}

#define WSTR_(x) L ## x
#define WSTR(x) WSTR_(x)

#define LogDebug(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::Debug , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )

#define LogInfo(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::Info , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )

#define LogWarning(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::Warning , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )

#define LogError(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::Error , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )

#define LogFatal(...) \
	sab::Logger::GetInstance().WriteLog( sab::Logger::Fatal , \
	WSTR( __FILE__ ) , __LINE__ , __VA_ARGS__ )