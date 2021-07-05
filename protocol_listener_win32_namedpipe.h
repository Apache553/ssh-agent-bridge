
#pragma once

#include "protocol_listener_base.h"

#include <Windows.h>

#include <string>
#include <list>
#include <atomic>

#include "protocol_ssh_helper.h"

namespace sab
{

	class Win32NamedPipeListener : public ProtocolListenerBase
	{
	public:
		struct PipeContext
		{
			enum class Status
			{
				Ready,
				ReadHeader,
				ReadBody,
				Write,
				Destroyed
			};

			HANDLE pipeHandle;
			Status pipeStatus;

			SshMessageEnvelope message;

			std::list<PipeContext>::iterator selfIterator;
		};

		static constexpr size_t MAX_PIPE_BUFFER_SIZE = 4 * 1024;
	private:
		std::wstring pipePath;

		std::list<PipeContext> contextList;

		std::atomic<bool> cancelFlag = false;
		HANDLE cancelEvent = NULL;
	public:
		Win32NamedPipeListener(const std::wstring& pipePath);

		void Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		virtual ~Win32NamedPipeListener();

	};
}
