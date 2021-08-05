
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <functional>

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
	
}