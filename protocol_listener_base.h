
#pragma once

#include <memory>

namespace sab
{

	/// <summary>
	/// Base class of listeners
	/// </summary>
	class ProtocolListenerBase:public std::enable_shared_from_this<ProtocolListenerBase>
	{
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
		
		ProtocolListenerBase() = default;
		ProtocolListenerBase(ProtocolListenerBase&&) = default;
		ProtocolListenerBase(const ProtocolListenerBase&) = delete;

		ProtocolListenerBase& operator=(ProtocolListenerBase&&) = default;
		ProtocolListenerBase& operator=(const ProtocolListenerBase&) = delete;
		
		virtual ~ProtocolListenerBase() = default;
	};
	
}