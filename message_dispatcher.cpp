
#include "message_dispatcher.h"

#include <cassert>

sab::MessageDispatcher::MessageDispatcher()
	:cancelFlag(false)
{
}

void sab::MessageDispatcher::PostRequest(SshMessageEnvelope* message, std::shared_ptr<void> holdKey)
{
	std::lock_guard<std::mutex> lg(listMutex);
	if(cancelFlag)
	{
		message->replyCallback(message, false);
		return;
	}
	messageList.emplace_back(message, holdKey);
	wakeCondition.notify_one();
}

void sab::MessageDispatcher::SetActiveClient(std::shared_ptr<ProtocolClientBase> client)
{
	activeClient = client;
}

bool sab::MessageDispatcher::Start()
{
	assert(activeClient != nullptr);
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
					bool status = activeClient->SendSshMessage(msg.first);
					msg.first->replyCallback(msg.first, status);
					lk.lock();
				}
				wakeCondition.wait(lk, [this]()
					{
						return !messageList.empty();
					});
			}
		});
	return true;
}

void sab::MessageDispatcher::Stop()
{
	std::lock_guard<std::mutex> lg(listMutex);
	cancelFlag = true;
	wakeCondition.notify_one();
	if (workerThread.joinable())
		workerThread.join();
}

sab::MessageDispatcher::~MessageDispatcher()
{
	Stop();
	std::lock_guard<std::mutex> lg(listMutex);
	while(!messageList.empty())
	{
		Message& msg = messageList.back();
		msg.first->replyCallback(msg.first, false);
		messageList.pop_back();
	}
}
