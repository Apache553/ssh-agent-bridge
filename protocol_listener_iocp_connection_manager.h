
#pragma once

#include "protocol_ssh_helper.h"
#include "protocol_listener_base.h"

#include <atomic>
#include <memory>
#include <list>
#include <mutex>
#include <thread>
#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{
	static constexpr size_t MAX_BUFFER_SIZE = 4 * 1024;
	
	class IocpListenerConnectionManager;

	class ListenerConnectionData
	{
	public:
		virtual ~ListenerConnectionData() = default;

		ListenerConnectionData() = default;
		ListenerConnectionData(const ListenerConnectionData&) = delete;
		ListenerConnectionData(ListenerConnectionData&&) = delete;

		ListenerConnectionData& operator=(const ListenerConnectionData&) = delete;
		ListenerConnectionData& operator=(ListenerConnectionData&&) = delete;
	};

	class IoContext :public std::enable_shared_from_this<IoContext>
	{
	public:

		enum class State
		{
			/// <summary>
			/// after construction
			/// </summary>
			Initialized = 0,

			/// <summary>
			/// connection was added to manager, waiting to do handshake
			/// </summary>
			Handshake,
			
			/// <summary>
			/// ready for next message
			/// </summary>
			Ready,
			
			/// <summary>
			/// reading header
			/// </summary>
			ReadHeader,

			/// <summary>
			/// reading body
			/// </summary>
			ReadBody,

			/// <summary>
			/// waiting for reply
			/// </summary>
			WaitReply,

			/// <summary>
			/// writing reply back
			/// </summary>
			WriteReply,

			/// <summary>
			/// connection ends, but the context is still referenced by someone else
			/// </summary>
			Destroyed
		};
		/*
		 * Initialized -> Handshake -> Ready -> ReadHeader -> ReadBody -> WaitReply -> WriteReply
		 *                               A                                                 |
		 *                               +-------------------------------------------------+
		 * Any state can go `Destroyed` when exception occurred/connection closes
		 */
	public:
		/// <summary>
		/// io handle
		/// </summary>
		HANDLE handle;

		/// <summary>
		/// OVERLAPPED structure for iocp
		/// </summary>
		OVERLAPPED overlapped;

		/// <summary>
		/// state
		/// </summary>
		State state;

		/// <summary>
		/// message store
		/// </summary>
		SshMessageEnvelope message;

		/// <summary>
		/// offest in message envelope
		/// </summary>
		int ioDataOffest;

		/// <summary>
		/// offest in io buffer
		/// </summary>
		int ioBufferOffest;

		/// <summary>
		/// transaction size
		/// </summary>
		int ioNeedBytes;

		/// <summary>
		/// buffer
		/// </summary>
		char ioBuffer[MAX_BUFFER_SIZE];
	public:
		/// <summary>
		/// listener of the connection
		/// </summary>
		std::shared_ptr<ProtocolListenerBase> listener;

		/// <summary>
		/// listener data
		/// </summary>
		std::shared_ptr<ListenerConnectionData> listenerData;
	public:
		/// <summary>
		/// flag to prevent from cleaned from context list
		/// </summary>
		std::atomic<int> holdFlag;

		/// <summary>
		/// iterator in context list
		/// </summary>
		std::list<std::shared_ptr<IoContext>>::iterator selfIter;

		/// <summary>
		/// connection manager owns the context
		/// </summary>
		std::shared_ptr<IocpListenerConnectionManager> owner;
	public:

		IoContext();
		IoContext(const IoContext&) = delete;
		IoContext(IoContext&&) = delete;

		IoContext& operator=(const IoContext&) = delete;
		IoContext& operator=(IoContext&&) = delete;

		~IoContext();
	public:
		/// <summary>
		/// dispose this io context
		/// </summary>
		void Dispose();

		/// <summary>
		/// called before any io or wait reply
		/// </summary>
		void PrepareIo();
		/// <summary>
		/// called after io completes or get reply
		/// </summary>
		void DoneIo();
	};

	class IocpListenerConnectionManager
		:public std::enable_shared_from_this<IocpListenerConnectionManager>
	{
	private:
		/// <summary>
		/// event handle to cancel worker thread
		/// </summary>
		HANDLE cancelEvent;

		/// <summary>
		/// handle of the completion port
		/// </summary>
		HANDLE iocpHandle;

		/// <summary>
		/// worker thread
		/// </summary>
		std::thread iocpThread;

		/// <summary>
		/// list of active connections
		/// </summary>
		std::list<std::shared_ptr<IoContext>> contextList;

		/// <summary>
		/// initialie flag
		/// </summary>
		bool initialized;

		/// <summary>
		/// mutex to sync access to contextList
		/// </summary>
		std::mutex listMutex;

		/// <summary>
		/// callback to emit a message
		/// </summary>
		std::function<void(SshMessageEnvelope*)> receiveCallback;
	public:
		IocpListenerConnectionManager();
		IocpListenerConnectionManager(const IocpListenerConnectionManager&) = delete;
		IocpListenerConnectionManager(IocpListenerConnectionManager&&) = delete;

		IocpListenerConnectionManager& operator=(const IocpListenerConnectionManager&) = delete;
		IocpListenerConnectionManager& operator=(IocpListenerConnectionManager&&) = delete;

		~IocpListenerConnectionManager();
	public:
		/// <summary>
		/// actually initialize the manager
		/// </summary>
		/// <returns>true for success, false for failure</returns>
		bool Initialize();

		/// <summary>
		/// start worker thread
		/// </summary>
		/// <returns>true for success, false for failure</returns>
		bool Start();

		/// <summary>
		/// stop worker thread (if is running)
		/// </summary>
		void Stop();

		/// <summary>
		/// delegate a connection which is ready for io to manager
		/// </summary>
		/// <param name="connection">connection handle</param>
		/// <param name="listener">listener instance</param>
		/// <param name="data">listener specific data</param>
		/// <returns>true for success, false for failure</returns>
		bool DelegateConnection(HANDLE connection,
			std::shared_ptr<ProtocolListenerBase> listener,
			std::shared_ptr<ListenerConnectionData> data);

		/// <summary>
		/// set message handler when received a message
		/// </summary>
		/// <param name="callback">the callback function</param>
		void SetEmitMessageCallback(std::function<void(SshMessageEnvelope*)>&& callback);

		/// <summary>
		/// post the reply of a message
		/// </summary>
		/// <param name="message">the message</param>
		/// <param name="status">indicate whether the reply was successfully put into 'message'</param>
		void PostMessageReply(SshMessageEnvelope* message, bool status);
	private:
		/// <summary>
		/// remove context from context list
		/// </summary>
		/// <param name="context"></param>
		void RemoveContext(IoContext* context);

		/// <summary>
		/// worker thread function
		/// </summary>
		void IocpThreadProc();

		/// <summary>
		/// actually process io operation
		/// </summary>
		/// <param name="context">the context</param>
		/// <param name="noRealIo">set to true to not get completion status</param>
		void DoIoCompletion(std::shared_ptr<IoContext> context, bool noRealIo);

		/// <summary>
		/// make IoContext our friend
		/// </summary>
		friend class IoContext;
	};

	class IIocpListener
	{
	public:
		/// <summary>
		/// do handshake
		/// </summary>
		/// <param name="context">the io context</param>
		/// <returns>true for completed handshake, else false</returns>
		virtual bool DoHandshake(std::shared_ptr<IoContext> context, int transferred) = 0;

		virtual ~IIocpListener() = default;
	};

}