
#pragma once

#include "protocol_ssh_helper.h"

#include <memory>
#include <functional>

namespace sab
{

	/// <summary>
	/// Base class of listeners
	/// all listeners can only be used with shared_ptr
	/// </summary>
	class ProtocolListenerBase :public std::enable_shared_from_this<ProtocolListenerBase>
	{
	protected:
		std::function<void(SshMessageEnvelope*)> receiveCallback;
	public:
		/// <summary>
		/// Run the listener, this method will be called in a standalone thread.
		/// </summary>
		virtual void Run() = 0;

		/// <summary>
		/// Cancel currently running listener, returns until listener is cancelled
		/// </summary>
		virtual void Cancel() = 0;

		/// <summary>
		/// Check if there is a cancel request
		/// </summary>
		/// <returns>true if cancel request present</returns>
		virtual bool IsCancelled()const = 0;

		/// <summary>
		/// Post a reply message
		/// </summary>
		virtual void PostBackReply(SshMessageEnvelope* message, bool status) = 0;

		/// <summary>
		/// set callback when a message comes
		/// </summary>
		/// <param name="callback">the callback</param>
		virtual void ImbueReceiveMessageCallback(std::function<void(SshMessageEnvelope*)>&& callback);

		ProtocolListenerBase() = default;
		ProtocolListenerBase(ProtocolListenerBase&&) = default;
		ProtocolListenerBase(const ProtocolListenerBase&) = delete;

		ProtocolListenerBase& operator=(ProtocolListenerBase&&) = default;
		ProtocolListenerBase& operator=(const ProtocolListenerBase&) = delete;

		virtual ~ProtocolListenerBase() = default;
	};

}