
#pragma once

#include "../client_base.h"

namespace sab
{
	class PageantClient: public ProtocolClientBase
	{
	public:
		static constexpr size_t MAX_PAGEANT_MESSAGE_SIZE = 8192;
		static constexpr unsigned int AGENT_COPYDATA_ID = 0x804e50ba;
		
		PageantClient();
		~PageantClient();

		bool SendSshMessage(SshMessageEnvelope* message)override;
	};
}