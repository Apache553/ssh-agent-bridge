
#pragma once

#include "../protocol_ssh_helper.h"
#include "../listener_base.h"
#include "../connection_manager.h"

#include <atomic>
#include <memory>
#include <list>
#include <mutex>
#include <thread>
#include <functional>

namespace sab
{

	class ProxyIoContext
		:public IoContext
	{
	public:
		enum class State
		{
			Initialized = 0,
			Handshake,
			Ready,
			ReadHeader,
			ReadBody,
			WaitReply,
			WriteReply,
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
		/**
		 * @brief OVERLAPPED structure for async i/o
		 */
		OVERLAPPED overlapped;
		
		/**
		 * @brief current state
		 */
		State state;

		/**
		 * @brief ssh agent message
 		 */
		SshMessageEnvelope message;

		/**
		 * @brief offset in agent message
	  	 */
		int ioDataOffset;

		/**
		 * @brief offset in buffer
		 */
		int ioBufferOffset;

		/**
		 * @brief remaining bytes to complete the transaction
		 */
		int ioNeedBytes;

		/**
		 * @brief buffer
		 */
		char ioBuffer[MAX_BUFFER_SIZE];
	public:
		ProxyIoContext();

		~ProxyIoContext();
		
		void Dispose()override;
	};
	
	class ProxyConnectionManager
		:public IConnectionManager
	{
	private:
		std::atomic<bool> cancelFlag;

		/**
		 * @brief completion port handle
		 */
		HANDLE iocpHandle;

		/**
		 * @brief worker thread
		 */
		std::thread iocpThread;

		/**
		 * @brief list of active connections
		 */
		std::list<std::shared_ptr<IoContext>> contextList;

		/**
		 * @brief initialized flag
		 */
		bool initialized;

		/**
		 * @brief synchronize access to list
		 */
		std::mutex listMutex;

		/**
		 * @brief callback to process received message
		 */
		std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)> receiveCallback;
	public:
		ProxyConnectionManager();

		~ProxyConnectionManager();
	public:

		bool Initialize();

		bool Start()override;

		void Stop()override;

		/**
		 * @brief delegate a inbound connection to connection manager for further handling
		 * @param connection handle of connection
		 * @param listener listener of connection
		 * @param data listener specific data
		 * @param isSocket indicates if the connection is a WSA SOCKET
		 * @return operation result, true stands for success
		 */
		bool DelegateConnection(HANDLE connection,
			std::shared_ptr<ProtocolListenerBase> listener,
			std::shared_ptr<ListenerConnectionData> data,
			bool isSocket)override;

		/**
		 * @brief set callback which will be called when a message was received
		 * @param callback the callback
		 */
		void SetEmitMessageCallback(std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)>&& callback);

		/**
		 * @brief remove a connection from connection list
		 * @param context the context
		 */
		void RemoveContext(IoContext* context)override;

	private:
		
		void IocpThreadProc();

		void DoIoCompletion(std::shared_ptr<ProxyIoContext> context, DWORD transferred);

		/**
		 * @brief helper function to send reply to connection initiator
		 * @param genericContext the context
		 * @param message the message
		 * @param status the result of getting reply
		 */
		void PostMessageReply(std::shared_ptr<void> genericContext,
			SshMessageEnvelope* message, bool status);
	
	};

}