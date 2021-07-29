
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

	class IocpListenerConnectionManager
		:public IConnectionManager
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
		std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)> receiveCallback;
	public:
		IocpListenerConnectionManager();

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
			std::shared_ptr<ListenerConnectionData> data,
			bool isSocket)override;

		/// <summary>
		/// set message handler when received a message
		/// </summary>
		/// <param name="callback">the callback function</param>
		void SetEmitMessageCallback(std::function<void(SshMessageEnvelope*, std::shared_ptr<void>)>&& callback);

	protected:
		/// <summary>
		/// remove context from context list
		/// </summary>
		/// <param name="context"></param>
		void RemoveContext(IoContext* context)override;

	private:
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
		/// Helper function for callback
		/// </summary>
		/// <param name="weakContext">weak_ptr of IoContext</param>
		/// <param name="message">the message</param>
		/// <param name="status">operation status</param>
		void PostMessageReply(std::shared_ptr<void> genericContext,
			SshMessageEnvelope* message, bool status);
	};

}