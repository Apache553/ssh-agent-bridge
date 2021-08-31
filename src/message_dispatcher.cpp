

#include "log.h"
#include "message_dispatcher.h"
#include "protocol/protocol_ssh_agent.h"

sab::MessageDispatcher::MessageDispatcher()
	:cancelFlag(false)
{
}

void sab::MessageDispatcher::PostRequest(SshMessageEnvelope* message, std::shared_ptr<void> holdKey)
{
	std::lock_guard<std::mutex> lg(listMutex);
	if (cancelFlag)
	{
		message->replyCallback(message, false);
		return;
	}
	messageList.emplace_back(message, holdKey);
	wakeCondition.notify_one();
}

void sab::MessageDispatcher::AddClient(std::shared_ptr<ProtocolClientBase> client)
{
	clients.emplace_back(std::move(client));
}

bool sab::MessageDispatcher::Start()
{
	workerThread = std::thread([this]()
		{
			std::unique_lock<std::mutex> lk(listMutex);
			while (!cancelFlag)
			{
				while (!messageList.empty())
				{
					Message msg = messageList.back();
					messageList.pop_back();
					lk.unlock();
					bool status = ProcessRequest(*msg.first);
					msg.first->replyCallback(msg.first, status);
					lk.lock();
				}
				wakeCondition.wait(lk, [this]()
					{
						return !messageList.empty() || cancelFlag;
					});
			}
		});
	return true;
}

void sab::MessageDispatcher::Stop()
{
	{
		std::lock_guard<std::mutex> lg(listMutex);
		cancelFlag = true;
		wakeCondition.notify_one();
	}
	if (workerThread.joinable())
		workerThread.join();
}

sab::MessageDispatcher::~MessageDispatcher()
{
	Stop();
	std::lock_guard<std::mutex> lg(listMutex);
	while (!messageList.empty())
	{
		Message& msg = messageList.back();
		msg.first->replyCallback(msg.first, false);
		messageList.pop_back();
	}
}

bool sab::MessageDispatcher::ProcessRequest(SshMessageEnvelope& envelope)
{
	if (envelope.length == 0)
		return false;

	char type = envelope.data[0];

	switch (type)
	{
	case SSH2_AGENTC_ADD_IDENTITY:
		return HandleAddIdentity(envelope);
	case SSH2_AGENTC_REMOVE_IDENTITY:
		return HandleRemoveIdentity(envelope);
	case SSH2_AGENTC_REMOVE_ALL_IDENTITIES:
		return HandleRemoveAllIdentity(envelope);
	case SSH2_AGENTC_REQUEST_IDENTITIES:
		return HandleIdentitiesRequest(envelope);
	case SSH2_AGENTC_SIGN_REQUEST:
		return HandleSignRequest(envelope);
	default:
		return HandleUnsupportedRequest(envelope);
	}
}

bool sab::MessageDispatcher::HandleAddIdentity(SshMessageEnvelope& envelope)
{
	// Send the request first defined upstream
	auto& client = clients.front();
	return client->SendSshMessage(&envelope);
}

bool sab::MessageDispatcher::HandleRemoveIdentity(SshMessageEnvelope& envelope)
{
	// Iterate all upstream until request succeeds
	for (auto& client : clients)
	{
		SshMessageEnvelope tmpMessage{ envelope };
		bool status = client->SendSshMessage(&tmpMessage);
		if (status)
		{
			if (tmpMessage.length > 0 && tmpMessage.data[0] == SSH_AGENT_SUCCESS)
			{
				envelope.length = tmpMessage.length;
				envelope.data = std::move(tmpMessage.data);
				return true;
			}
		}
	}
	SshAgentMessageBufferWriter writer(envelope);
	writer.Init();
	SshAgentMessageGenericFailure{}.ToBuffer(writer);
	return true;
}

bool sab::MessageDispatcher::HandleRemoveAllIdentity(SshMessageEnvelope& envelope)
{
	// Broadcast to all upstreams
	for (auto& client : clients)
	{
		SshMessageEnvelope tmpMessage{ envelope };
		client->SendSshMessage(&tmpMessage);
	}
	SshAgentMessageBufferWriter writer(envelope);
	writer.Init();
	SshAgentMessageGenericSuccess{}.ToBuffer(writer);
	return true;
}

bool sab::MessageDispatcher::HandleIdentitiesRequest(SshMessageEnvelope& envelope)
{
	// Iterate all upstream then summarize
	SshAgentMessageRequestIdentitiesAnswer ans;
	for (auto& client : clients)
	{
		LogDebug(L"try get indentities...");
		SshMessageEnvelope tmpMessage{ envelope };
		bool status = client->SendSshMessage(&tmpMessage);
		if (status)
		{
			if (tmpMessage.length > 0 && tmpMessage.data[0] == SSH2_AGENT_IDENTITIES_ANSWER)
			{
				SshAgentMessageRequestIdentitiesAnswer partialAns;
				SshAgentMessageBufferReader reader(tmpMessage);
				if (partialAns.FromBuffer(reader))
				{
					LogDebug(L"get ", partialAns.identities.size(), L" indentities.");
					ans.identities.insert(
						ans.identities.end(),
						std::make_move_iterator(partialAns.identities.begin()),
						std::make_move_iterator(partialAns.identities.end()));
				}
			}
		}
	}
	LogDebug(L"assemble reply message, ", ans.identities.size(), L" identities included.");
	SshAgentMessageBufferWriter writer(envelope);
	writer.Init();
	ans.ToBuffer(writer);
	return true;
}

bool sab::MessageDispatcher::HandleSignRequest(SshMessageEnvelope& envelope)
{
	// Iterate all upstream until request succeeds
	for (auto& client : clients)
	{
		LogDebug(L"try signing...");
		SshMessageEnvelope tmpMessage{ envelope };
		bool status = client->SendSshMessage(&tmpMessage);
		if (status)
		{
			if (tmpMessage.length > 0 && tmpMessage.data[0] == SSH2_AGENT_SIGN_RESPONSE)
			{
				LogDebug(L"sign done.");
				envelope.length = tmpMessage.length;
				envelope.data = std::move(tmpMessage.data);
				return true;
			}
		}
	}
	LogDebug(L"all sign attemption failed!");
	SshAgentMessageBufferWriter writer(envelope);
	writer.Init();
	SshAgentMessageGenericFailure{}.ToBuffer(writer);
	return true;
}

bool sab::MessageDispatcher::HandleUnsupportedRequest(SshMessageEnvelope& envelope)
{
	// Fail all operations unsupported
	SshAgentMessageBufferWriter writer(envelope);
	writer.Init();
	SshAgentMessageGenericFailure{}.ToBuffer(writer);
	return true;
}
