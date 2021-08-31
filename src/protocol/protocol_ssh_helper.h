
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace sab
{
	/// <summary>
	/// Forward declaration
	/// </summary>
	class ProtocolListenerBase;

	struct SshMessageEnvelope
	{
		/// <summary>
		/// the length of the message, native byte order
		/// </summary>
		uint32_t length;

		/// <summary>
		/// data contained
		/// </summary>
		std::vector<uint8_t> data;

		/// <summary>
		/// store the callback used when sending reply.
		/// if stored a lambda, it must not contain a shared_ptr to itself!!!
		/// or memory leak will occur
		/// </summary>
		std::function<void(SshMessageEnvelope*, bool)> replyCallback;
	};

	static constexpr size_t MAX_MESSAGE_SIZE = 256 * 1024;
	static constexpr size_t HEADER_SIZE = sizeof(uint32_t);

	class SshAgentMessageBufferReader
	{
	private:
		const SshMessageEnvelope& envelope;
		size_t next = 0;

		bool CanConsume(size_t bytes)const;
	public:
		void Reset();
		bool ReadByte(char& data);
		bool ReadBoolean(bool& data);
		bool ReadUInt32(uint32_t& data);
		bool ReadUInt64(uint64_t& data);
		bool ReadString(std::string& data);

		SshAgentMessageBufferReader(const SshMessageEnvelope& envelope)
			:envelope(envelope) {}
	};

	class SshAgentMessageBufferWriter
	{
	private:
		SshMessageEnvelope& envelope;
	public:
		void Init();
		void WriteByte(char data);
		void WriteBool(bool data);
		void WriteUInt32(uint32_t data);
		void WriteUInt64(uint64_t data);
		void WriteString(const std::string& data);

		SshAgentMessageBufferWriter(SshMessageEnvelope& envelope)
			:envelope(envelope) {}
	};
}
