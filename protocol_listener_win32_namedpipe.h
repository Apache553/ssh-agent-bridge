
#pragma once

#include "util.h"
#include "protocol_ssh_helper.h"
#include "protocol_listener_base.h"

#include <atomic>
#include <string>
#include <list>
#include <mutex>

#include <Windows.h>


namespace sab
{

	class Win32NamedPipeListener : public ProtocolListenerBase
	{
	public:
		static constexpr size_t MAX_PIPE_BUFFER_SIZE = 4 * 1024;

		struct PipeContext
		{
			/// <summary>
			/// Indicates current pipe status
			/// </summary>
			enum class Status
			{
				Initialized,
				Listening,
				ReadHeader,
				ReadBody,
				WaitReply,
				Write,
				Destroyed
			};

			OVERLAPPED overlapped;
			HANDLE pipeHandle;
			Status pipeStatus;

			SshMessageEnvelope message;

			std::list<std::shared_ptr<PipeContext>>::iterator selfIterator;

			char buffer[MAX_PIPE_BUFFER_SIZE];
			int needTransferBytes;
			int transferredBytes;

			bool externalReference;
			bool disposed;

		public:

			PipeContext();
			PipeContext(const PipeContext&) = delete;
			PipeContext(PipeContext&&) = delete;

			PipeContext& operator=(const PipeContext&) = delete;
			PipeContext& operator=(PipeContext&&) = delete;

			~PipeContext();

		};

	private:
		std::wstring pipePath;

		std::mutex listMutex;
		std::list<std::shared_ptr<PipeContext>> contextList;

		HANDLE cancelEvent = NULL;
	public:
		Win32NamedPipeListener(const std::wstring& pipePath);

		void Run()override;

		void Cancel()override;

		bool IsCancelled()const override;

		void PostBackReply(SshMessageEnvelope*, bool status)override;

		~Win32NamedPipeListener()override;
	private:
		bool ListenLoop();

		void OnIoCompletion(PipeContext* context, bool noRealIo = false);
		void FinalizeContext(PipeContext* context);
		void IocpThreadProc(HANDLE iocpHandle);
	};
}
