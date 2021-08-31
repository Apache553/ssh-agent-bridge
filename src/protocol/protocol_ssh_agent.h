
#pragma once

#include "protocol_ssh_helper.h"

#include <string>
#include <vector>
#include <cstdint>

namespace sab
{
	static constexpr char SSH_AGENT_FAILURE = 5;
	static constexpr char SSH_AGENT_SUCCESS = 6;

	static constexpr char SSH2_AGENTC_REQUEST_IDENTITIES = 11;
	static constexpr char SSH2_AGENT_IDENTITIES_ANSWER = 12;
	static constexpr char SSH2_AGENTC_SIGN_REQUEST = 13;
	static constexpr char SSH2_AGENT_SIGN_RESPONSE = 14;
	static constexpr char SSH2_AGENTC_ADD_IDENTITY = 17;
	static constexpr char SSH2_AGENTC_REMOVE_IDENTITY = 18;
	static constexpr char SSH2_AGENTC_REMOVE_ALL_IDENTITIES = 19;

	class SshAgentIdentity
	{
	public:
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	public:
		std::string blob;
		std::string comment;
	};

	class SshAgentDsaKey
	{
	public:
		static constexpr auto TYPE_PREFIX = "ssh-dss";
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	public:
		std::string p;
		std::string q;
		std::string g;
		std::string y;
		std::string x;
	};

	class SshAgentEcdsaKey
	{
	public:
		static constexpr auto TYPE_PREFIX = "ecdsa-sha2-";
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	public:
		std::string ecdsaCurveName;
		std::string Q;
		std::string d;
	};

	class SshAgentEd25519Key
	{
	public:
		static constexpr auto TYPE_PREFIX = "ssh-ed25519";
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	public:
		std::string encA;
		std::string kEncA;
	};

	class SshAgentRsaKey
	{
	public:
		static constexpr auto TYPE_PREFIX = "ssh-rsa";
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	public:
		std::string n;
		std::string e;
		std::string d;
		std::string iqmp;
		std::string p;
		std::string q;
	};

	class SshAgentMessageGenericSuccess
	{
	public:
		static constexpr char ID = SSH_AGENT_SUCCESS;
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	};

	class SshAgentMessageGenericFailure
	{
	public:
		static constexpr char ID = SSH_AGENT_FAILURE;
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	};

	class SshAgentMessageRequestIdentities
	{
	public:
		static constexpr char ID = SSH2_AGENTC_REQUEST_IDENTITIES;
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	};

	class SshAgentMessageRequestIdentitiesAnswer
	{
	public:
		static constexpr char ID = SSH2_AGENT_IDENTITIES_ANSWER;
		bool FromBuffer(SshAgentMessageBufferReader& reader);
		void ToBuffer(SshAgentMessageBufferWriter& writer)const;
	public:
		std::vector<SshAgentIdentity> identities;

	};



}