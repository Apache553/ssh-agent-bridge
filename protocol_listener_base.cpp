
#include "protocol_listener_base.h"

void sab::ProtocolListenerBase::ImbueReceiveMessageCallback(std::function<void(SshMessageEnvelope*)>&& callback)
{
	receiveCallback = callback;
}
