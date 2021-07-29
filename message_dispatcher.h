
#pragma once

#include "protocol/protocol_ssh_helper.h"
#include "protocol/client_base.h"

#include <vector>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <thread>

namespace sab
{
	class MessageDispatcher
	{
	public:
		using Message = std::pair<SshMessageEnvelope*, std::shared_ptr<void>>;
	private:
		std::vector<Message> messageList;

		std::mutex listMutex;
		std::condition_variable wakeCondition;

		bool cancelFlag;

		std::thread workerThread;

		std::shared_ptr<ProtocolClientBase> activeClient;
	public:
		MessageDispatcher();

		void PostRequest(SshMessageEnvelope* message, std::shared_ptr<void> holdKey);

		void SetActiveClient(std::shared_ptr<ProtocolClientBase> client);
		
		bool Start();

		void Stop();

		~MessageDispatcher();
	};
}
