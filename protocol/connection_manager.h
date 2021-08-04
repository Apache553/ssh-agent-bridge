
#pragma once

#include "listener_base.h"

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
		enum class HandleType
		{
			Invalid = 0,
			FileHandle,
			SocketHandle
		};
	public:
		/**
		 * @brief i/o handle
		*/
		HANDLE handle;

		/**
		 * @brief handle type
		*/
		HandleType handleType;
	public:
		/**
		 * @brief the listener delegates the connection
		*/
		std::shared_ptr<ProtocolListenerBase> listener;

		/**
		 * @brief listener specific data
		*/
		std::shared_ptr<ListenerConnectionData> listenerData;
		
		/**
		 * @brief iterator of the context in context list of connection manager
		*/
		std::list<std::shared_ptr<IoContext>>::iterator selfIter;

		/**
		 * @brief the connection manager this context belongs to
		*/
		std::shared_ptr<IConnectionManager> owner;
	public:

		IoContext();
		IoContext(const IoContext&) = delete;
		IoContext(IoContext&&) = delete;

		IoContext& operator=(const IoContext&) = delete;
		IoContext& operator=(IoContext&&) = delete;

		virtual ~IoContext();
	public:
		/**
		 * @brief remove the context from connection manager's list
		*/
		virtual void Dispose() = 0;

	public:
		static void CloseIoHandle(HANDLE handle, HandleType type);
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

		/// <summary>
		/// remove context from context list
		/// </summary>
		/// <param name="context"></param>
		virtual void RemoveContext(IoContext* context) = 0;
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