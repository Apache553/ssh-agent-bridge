
#include "protocol_ssh_helper.h"

#include <intrin.h>

static void ByteSwap(uint32_t& value)
{
	value = _byteswap_ulong(value);
}

static void ByteSwap(uint64_t& value)
{
	value = _byteswap_uint64(value);
}

bool sab::SshAgentMessageBufferReader::CanConsume(size_t bytes) const
{
	return next + bytes <= envelope.length;
}

void sab::SshAgentMessageBufferReader::Reset()
{
	next = 0;
}

bool sab::SshAgentMessageBufferReader::ReadByte(char& data)
{
	if (!CanConsume(sizeof(char)))
		return false;
	data = envelope.data[next];
	next += sizeof(char);
	return true;
}

bool sab::SshAgentMessageBufferReader::ReadBoolean(bool& data)
{
	if (!CanConsume(sizeof(char)))
		return false;
	data = envelope.data[next];
	next += sizeof(char);
	return true;
}

bool sab::SshAgentMessageBufferReader::ReadUInt32(uint32_t& data)
{
	if (!CanConsume(sizeof(uint32_t)))
		return false;
	memcpy(&data, envelope.data.data() + next, sizeof(uint32_t));
	ByteSwap(data);
	next += sizeof(uint32_t);
	return true;
}

bool sab::SshAgentMessageBufferReader::ReadUInt64(uint64_t& data)
{
	if (!CanConsume(sizeof(uint64_t)))
		return false;
	memcpy(&data, envelope.data.data() + next, sizeof(uint64_t));
	ByteSwap(data);
	next += sizeof(uint64_t);
	return true;
}

bool sab::SshAgentMessageBufferReader::ReadString(std::string& data)
{
	uint32_t length;
	if (!ReadUInt32(length) || !CanConsume(length))
	{
		next -= sizeof(uint32_t);
		return false;
	}
	data.assign(envelope.data.data() + next, envelope.data.data() + next + length);
	next += length;
	return true;
}

void sab::SshAgentMessageBufferWriter::Init()
{
	envelope.data.clear();
	envelope.length = 0;
}

void sab::SshAgentMessageBufferWriter::WriteByte(char data)
{
	envelope.data.push_back(data);
	envelope.length += 1;
}

void sab::SshAgentMessageBufferWriter::WriteBool(bool data)
{
	envelope.data.push_back(data ? 1 : 0);
	envelope.length += 1;
}

void sab::SshAgentMessageBufferWriter::WriteUInt32(uint32_t data)
{
	ByteSwap(data);
	const char* dptr = reinterpret_cast<const char*>(&data);
	envelope.data.insert(envelope.data.end(), dptr, dptr + sizeof(uint32_t));
	envelope.length += sizeof(uint32_t);
}

void sab::SshAgentMessageBufferWriter::WriteUInt64(uint64_t data)
{
	ByteSwap(data);
	auto dptr = reinterpret_cast<const char*>(&data);
	envelope.data.insert(envelope.data.end(), dptr, dptr + sizeof(uint64_t));
	envelope.length += sizeof(uint64_t);
}

void sab::SshAgentMessageBufferWriter::WriteString(const std::string& data)
{
	WriteUInt32(static_cast<uint32_t>(data.size()));
	envelope.data.insert(envelope.data.end(), data.begin(), data.end());
	envelope.length += data.size();
}
