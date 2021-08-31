
#include "protocol_ssh_agent.h"

bool sab::SshAgentIdentity::FromBuffer(SshAgentMessageBufferReader& reader)
{
	return reader.ReadString(blob)
		&& reader.ReadString(comment);
}

void sab::SshAgentIdentity::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteString(blob);
	writer.WriteString(comment);
}

bool sab::SshAgentDsaKey::FromBuffer(SshAgentMessageBufferReader& reader)
{
	return reader.ReadString(p)
		&& reader.ReadString(q)
		&& reader.ReadString(g)
		&& reader.ReadString(y)
		&& reader.ReadString(x);
}

void sab::SshAgentDsaKey::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteString(p);
	writer.WriteString(q);
	writer.WriteString(g);
	writer.WriteString(y);
	writer.WriteString(x);
}

bool sab::SshAgentEcdsaKey::FromBuffer(SshAgentMessageBufferReader& reader)
{
	return reader.ReadString(ecdsaCurveName)
		&& reader.ReadString(Q)
		&& reader.ReadString(d);
}

void sab::SshAgentEcdsaKey::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteString(ecdsaCurveName);
	writer.WriteString(Q);
	writer.WriteString(d);
}

bool sab::SshAgentEd25519Key::FromBuffer(SshAgentMessageBufferReader& reader)
{
	return reader.ReadString(encA)
		&& reader.ReadString(kEncA);
}

void sab::SshAgentEd25519Key::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteString(encA);
	writer.WriteString(kEncA);
}

bool sab::SshAgentRsaKey::FromBuffer(SshAgentMessageBufferReader& reader)
{
	return reader.ReadString(n)
		&& reader.ReadString(e)
		&& reader.ReadString(d)
		&& reader.ReadString(iqmp)
		&& reader.ReadString(p)
		&& reader.ReadString(q);
}

void sab::SshAgentRsaKey::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteString(n);
	writer.WriteString(e);
	writer.WriteString(d);
	writer.WriteString(iqmp);
	writer.WriteString(p);
	writer.WriteString(q);
}

bool sab::SshAgentMessageGenericSuccess::FromBuffer(SshAgentMessageBufferReader& reader)
{
	char id;
	if (reader.ReadByte(id) && id == ID)
		return true;
	return false;
}

void sab::SshAgentMessageGenericSuccess::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteByte(ID);
}

bool sab::SshAgentMessageGenericFailure::FromBuffer(SshAgentMessageBufferReader& reader)
{
	char id;
	if (reader.ReadByte(id) && id == ID)
		return true;
	return false;
}

void sab::SshAgentMessageGenericFailure::ToBuffer(SshAgentMessageBufferWriter& writer) const
{
	writer.WriteByte(ID);
}

bool sab::SshAgentMessageRequestIdentities::FromBuffer(SshAgentMessageBufferReader& reader)
{
	char id;
	if (reader.ReadByte(id) && id == ID)
		return true;
	return false;
}

void sab::SshAgentMessageRequestIdentities::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteByte(ID);
}

bool sab::SshAgentMessageRequestIdentitiesAnswer::FromBuffer(SshAgentMessageBufferReader& reader)
{
	char id;
	uint32_t count;
	identities.clear();
	if (reader.ReadByte(id) && id == ID && reader.ReadUInt32(count))
	{
		for (uint32_t i = 0; i < count; ++i)
		{
			SshAgentIdentity identity;
			if (!identity.FromBuffer(reader))
				return false;
			identities.emplace_back(std::move(identity));
		}
		return true;
	}
	return false;
}

void sab::SshAgentMessageRequestIdentitiesAnswer::ToBuffer(SshAgentMessageBufferWriter& writer)const
{
	writer.WriteByte(ID);
	writer.WriteUInt32(static_cast<uint32_t>(identities.size()));
	for (const auto& identity : identities)
	{
		identity.ToBuffer(writer);
	}
}
