
#pragma once

#include "protocol_ssh_helper.h"

namespace sab
{
	class ProtocolClientBase :public std::enable_shared_from_this<ProtocolClientBase>
	{
	public:
		/// <summary>
		/// send message to get reply
		/// </summary>
		/// <param name="message">message envelope, both request and reply data</param>
		/// <returns>indicate whether the operation is successful</returns>
		virtual bool SendSshMessage(SshMessageEnvelope* message) = 0;

		ProtocolClientBase() = default;
		ProtocolClientBase(ProtocolClientBase&&) = default;
		ProtocolClientBase(const ProtocolClientBase&) = delete;

		ProtocolClientBase& operator=(ProtocolClientBase&&) = default;
		ProtocolClientBase& operator=(const ProtocolClientBase&) = delete;
		
		virtual ~ProtocolClientBase() = default;
	};
}