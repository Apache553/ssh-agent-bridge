
#pragma once

#include "../client_base.h"

#include <string>

namespace sab
{
	class Win32NamedPipeClient:public ProtocolClientBase
	{
	private:
		std::wstring pipePath;
	public:
		Win32NamedPipeClient(const std::wstring& pipePath);

		bool SendSshMessage(SshMessageEnvelope* message)override;

		~Win32NamedPipeClient()override;
	};
}