
#pragma once

#include <cstdint>
#include <vector>
#include <memory>

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
		/// the source listener of the message
		/// </summary>
		std::shared_ptr<ProtocolListenerBase> source;
		/// <summary>
		/// custom data to identify transaction by source
		/// </summary>
		void* id;
	};

	static constexpr size_t MAX_MESSAGE_SIZE = 256 * 1024;

	
}