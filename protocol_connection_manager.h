
#pragma once

#include "protocol_listener_base.h"

#include <list>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sab
{
	static constexpr size_t MAX_BUFFER_SIZE = 4 * 1024;

	class IConnectionManager;
	
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
			Destroyed,
		};
		/*
		 * Normal Message:
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
		/// set to true if it is a socket
		/// </summary>
		bool isSocket;

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
		std::shared_ptr<IConnectionManager> owner;
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
		/// called before any io
		/// </summary>
		void PrepareIo();
		/// <summary>
		/// called after io completes
		/// </summary>
		void DoneIo();
	};

	class IConnectionManager
		:public std::enable_shared_from_this<IConnectionManager>
	{
	public:
		IConnectionManager() = default;
		IConnectionManager(const IConnectionManager&) = delete;
		IConnectionManager(IConnectionManager&&) = delete;

		IConnectionManager& operator=(const IConnectionManager&) = delete;
		IConnectionManager& operator=(IConnectionManager&&) = delete;

		virtual ~IConnectionManager() = default;

		/// <summary>
		/// actually initialize the manager
		/// </summary>
		/// <returns>true for success, false for failure</returns>
		virtual bool Initialize() = 0;

		/// <summary>
		/// start worker thread
		/// </summary>
		/// <returns>true for success, false for failure</returns>
		virtual bool Start() = 0;

		/// <summary>
		/// stop worker thread (if is running)
		/// </summary>
		virtual void Stop() = 0;

		/// <summary>
		/// delegate a connection which is ready for io to manager
		/// </summary>
		/// <param name="connection">connection handle</param>
		/// <param name="listener">listener instance</param>
		/// <param name="data">listener specific data</param>
		/// <returns>true for success, false for failure</returns>
		virtual bool DelegateConnection(HANDLE connection,
			std::shared_ptr<ProtocolListenerBase> listener,
			std::shared_ptr<ListenerConnectionData> data,
			bool isSocket) = 0;
	protected:
		/// <summary>
		/// remove context from context list
		/// </summary>
		/// <param name="context"></param>
		virtual void RemoveContext(IoContext* context) = 0;

		/// <summary>
		/// make IoContext our friend
		/// </summary>
		friend class IoContext;
	};

	class IManagedListener
	{
	public:
		/// <summary>
		/// do handshake
		/// </summary>
		/// <param name="context">the io context</param>
		/// <returns>true for completed handshake, else false</returns>
		virtual bool DoHandshake(std::shared_ptr<IoContext> context, int transferred) = 0;

		virtual ~IManagedListener() = default;
	};
}