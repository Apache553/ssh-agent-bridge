
#pragma once

#include "protocol_client_base.h"

#include <string>

namespace sab
{
	/*
	 * GnuPG assuan socket for ssh is presently broken
	 * so it is UESLESS until GnuPG fixed the problem
	 */
	class LibassuanSocketEmulationClient : public ProtocolClientBase
	{
	private:
		std::wstring pipePath;
	public:

		LibassuanSocketEmulationClient(const std::wstring& pipePath);
		~LibassuanSocketEmulationClient();

		bool SendSshMessage(SshMessageEnvelope* message)override;
	};
}