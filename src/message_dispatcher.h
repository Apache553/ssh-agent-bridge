
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

		std::vector<std::shared_ptr<ProtocolClientBase>> clients;

		bool mangleCommentFlag;
	public:
		MessageDispatcher();

		void PostRequest(SshMessageEnvelope* message, std::shared_ptr<void> holdKey);

		void AddClient(std::shared_ptr<ProtocolClientBase> client);
		
		bool Start();

		void Stop();

		void SetKeyCommentMangling(bool flag);
		
		~MessageDispatcher();
	private:
		bool ProcessRequest(SshMessageEnvelope& envelope);

		bool HandleAddIdentity(SshMessageEnvelope& envelope);
		bool HandleRemoveIdentity(SshMessageEnvelope& envelope);
		bool HandleRemoveAllIdentity(SshMessageEnvelope& envelope);
		bool HandleIdentitiesRequest(SshMessageEnvelope& envelope);
		bool HandleSignRequest(SshMessageEnvelope& envelope);
		bool HandleUnsupportedRequest(SshMessageEnvelope& envelope);
	};
}
