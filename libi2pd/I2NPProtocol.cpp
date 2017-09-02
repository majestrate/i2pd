#include <string.h>
#include <atomic>
#include "Base.h"
#include "Log.h"
#include "Crypto.h"
#include "I2PEndian.h"
#include "Timestamp.h"
#include "RouterContext.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "Transports.h"
#include "Garlic.h"
#include "I2NPProtocol.h"
#include "version.h"

using namespace i2p::transport;

namespace i2p
{
	std::shared_ptr<I2NPMessage> NewI2NPMessage ()
	{
		return std::make_shared<I2NPMessageBuffer<I2NP_MAX_MESSAGE_SIZE> >();
	}

	std::shared_ptr<I2NPMessage> NewI2NPShortMessage ()
	{
		return std::make_shared<I2NPMessageBuffer<I2NP_MAX_SHORT_MESSAGE_SIZE> >();
	}

	std::shared_ptr<I2NPMessage> NewI2NPTunnelMessage ()
	{
		auto msg = new I2NPMessageBuffer<i2p::tunnel::TUNNEL_DATA_MSG_SIZE + I2NP_HEADER_SIZE + 34>(); // reserved for alignment and NTCP 16 + 6 + 12
		msg->Align (12);
		return std::shared_ptr<I2NPMessage>(msg);
	}

	std::shared_ptr<I2NPMessage> NewI2NPMessage (size_t len)
	{
		return (len < I2NP_MAX_SHORT_MESSAGE_SIZE/2) ? NewI2NPShortMessage () : NewI2NPMessage ();
	}

	void I2NPMessage::FillI2NPMessageHeader (I2NPMessageType msgType, uint32_t replyMsgID)
	{
		SetTypeID (msgType);
		if (!replyMsgID) RAND_bytes ((uint8_t *)&replyMsgID, 4);
		SetMsgID (replyMsgID);
		SetExpiration (i2p::util::GetMillisecondsSinceEpoch () + I2NP_MESSAGE_EXPIRATION_TIMEOUT);
		UpdateSize ();
		UpdateChks ();
	}

	void I2NPMessage::RenewI2NPMessageHeader ()
	{
		uint32_t msgID;
		RAND_bytes ((uint8_t *)&msgID, 4);
		SetMsgID (msgID);
		SetExpiration (i2p::util::GetMillisecondsSinceEpoch () + I2NP_MESSAGE_EXPIRATION_TIMEOUT);
	}

	bool I2NPMessage::IsExpired () const
	{
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		auto exp = GetExpiration ();
		return (ts > exp + I2NP_MESSAGE_CLOCK_SKEW) || (ts < exp - 3*I2NP_MESSAGE_CLOCK_SKEW); // check if expired or too far in future
	}

	std::shared_ptr<I2NPMessage> CreateI2NPMessage (I2NPMessageType msgType, const uint8_t * buf, size_t len, uint32_t replyMsgID)
	{
		auto msg = NewI2NPMessage (len);
		if (msg->Concat (buf, len) < len)
			LogPrint (eLogError, "I2NP: message length ", len, " exceeds max length ", msg->maxLen);
		msg->FillI2NPMessageHeader (msgType, replyMsgID);
		return msg;
	}

	std::shared_ptr<I2NPMessage> CreateI2NPMessage (const uint8_t * buf, size_t len, std::shared_ptr<i2p::tunnel::InboundTunnel> from)
	{
		auto msg = NewI2NPMessage ();
		if (msg->offset + len < msg->maxLen)
		{
			memcpy (msg->GetBuffer (), buf, len);
			msg->len = msg->offset + len;
			msg->from = from;
		}
		else
			LogPrint (eLogError, "I2NP: message length ", len, " exceeds max length");
		return msg;
	}

	std::shared_ptr<I2NPMessage> CopyI2NPMessage (std::shared_ptr<I2NPMessage> msg)
	{
		if (!msg) return nullptr;
		auto newMsg = NewI2NPMessage (msg->len);
		newMsg->offset = msg->offset;
		*newMsg = *msg;
		return newMsg;
	}


	std::shared_ptr<I2NPMessage> CreateRouterInfoDatabaseLookupMsg (const uint8_t * key, const uint8_t * from,
		uint32_t replyTunnelID, bool exploratory, std::set<i2p::data::IdentHash> * excludedPeers)
	{
		auto m = excludedPeers ? NewI2NPMessage () : NewI2NPShortMessage ();
		uint8_t * buf = m->GetPayload ();
		memcpy (buf, key, 32); // key
		buf += 32;
		memcpy (buf, from, 32); // from
		buf += 32;
		uint8_t flag = exploratory ? DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP : DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP;
		if (replyTunnelID)
		{
			*buf = flag | DATABASE_LOOKUP_DELIVERY_FLAG; // set delivery flag
			htobe32buf (buf+1, replyTunnelID);
			buf += 5;
		}
		else
		{
			*buf = flag; // flag
			buf++;
		}

		if (excludedPeers)
		{
			int cnt = excludedPeers->size ();
			htobe16buf (buf, cnt);
			buf += 2;
			for (auto& it: *excludedPeers)
			{
				memcpy (buf, it, 32);
				buf += 32;
			}
		}
		else
		{
			// nothing to exclude
			htobuf16 (buf, 0);
			buf += 2;
		}

		m->len += (buf - m->GetPayload ());
		m->FillI2NPMessageHeader (eI2NPDatabaseLookup);
		return m;
	}

	std::shared_ptr<I2NPMessage> CreateLeaseSetDatabaseLookupMsg (const i2p::data::IdentHash& dest,
		const std::set<i2p::data::IdentHash>& excludedFloodfills,
		std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel, const uint8_t * replyKey, const uint8_t * replyTag)
	{
		int cnt = excludedFloodfills.size ();
		auto m = cnt > 0 ? NewI2NPMessage () : NewI2NPShortMessage ();
		uint8_t * buf = m->GetPayload ();
		memcpy (buf, dest, 32); // key
		buf += 32;
		memcpy (buf, replyTunnel->GetNextIdentHash (), 32); // reply tunnel GW
		buf += 32;
		*buf = DATABASE_LOOKUP_DELIVERY_FLAG | DATABASE_LOOKUP_ENCRYPTION_FLAG | DATABASE_LOOKUP_TYPE_LEASESET_LOOKUP; // flags
		buf ++;
		htobe32buf (buf, replyTunnel->GetNextTunnelID ()); // reply tunnel ID
		buf += 4;

		// excluded
		htobe16buf (buf, cnt);
		buf += 2;
		if (cnt > 0)
		{
			for (auto& it: excludedFloodfills)
			{
				memcpy (buf, it, 32);
				buf += 32;
			}
		}
		// encryption
		memcpy (buf, replyKey, 32);
		buf[32] = uint8_t( 1 ); // 1 tag
		memcpy (buf + 33, replyTag, 32);
		buf += 65;

		m->len += (buf - m->GetPayload ());
		m->FillI2NPMessageHeader (eI2NPDatabaseLookup);
		return m;
	}


	std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (std::shared_ptr<const i2p::data::LeaseSet> leaseSet)
	{
		if (!leaseSet) return nullptr;
		auto m = NewI2NPShortMessage ();
		uint8_t * payload = m->GetPayload ();
		memcpy (payload + DATABASE_STORE_KEY_OFFSET, leaseSet->GetIdentHash (), 32);
		payload[DATABASE_STORE_TYPE_OFFSET] = 1; // LeaseSet
		htobe32buf (payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, 0);
		size_t size = DATABASE_STORE_HEADER_SIZE;
		memcpy (payload + size, leaseSet->GetBuffer (), leaseSet->GetBufferLen ());
		size += leaseSet->GetBufferLen ();
		m->len += size;
		m->FillI2NPMessageHeader (eI2NPDatabaseStore);
		return m;
	}

	std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (std::shared_ptr<const i2p::data::LocalLeaseSet> leaseSet,  uint32_t replyToken, std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel)
	{
		if (!leaseSet) return nullptr;
		auto m = NewI2NPShortMessage ();
		uint8_t * payload = m->GetPayload ();
		memcpy (payload + DATABASE_STORE_KEY_OFFSET, leaseSet->GetIdentHash (), 32);
		payload[DATABASE_STORE_TYPE_OFFSET] = 1; // LeaseSet
		htobe32buf (payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, replyToken);
		size_t size = DATABASE_STORE_HEADER_SIZE;
		if (replyToken && replyTunnel)
		{
			if (replyTunnel)
			{
				htobe32buf (payload + size, replyTunnel->GetNextTunnelID ());
				size += 4; // reply tunnelID
				memcpy (payload + size, replyTunnel->GetNextIdentHash (), 32);
				size += 32; // reply tunnel gateway
			}
			else
				htobe32buf (payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, 0);
		}
		memcpy (payload + size, leaseSet->GetBuffer (), leaseSet->GetBufferLen ());
		size += leaseSet->GetBufferLen ();
		m->len += size;
		m->FillI2NPMessageHeader (eI2NPDatabaseStore);
		return m;
	}

	bool IsRouterInfoMsg (std::shared_ptr<I2NPMessage> msg)
	{
		if (!msg || msg->GetTypeID () != eI2NPDatabaseStore) return false;
		return !msg->GetPayload ()[DATABASE_STORE_TYPE_OFFSET]; // 0- RouterInfo
	}


	std::shared_ptr<I2NPMessage> CreateTunnelDataMsg (const uint8_t * buf)
	{
		auto msg = NewI2NPTunnelMessage ();
		msg->Concat (buf, i2p::tunnel::TUNNEL_DATA_MSG_SIZE);
		msg->FillI2NPMessageHeader (eI2NPTunnelData);
		return msg;
	}

	std::shared_ptr<I2NPMessage> CreateTunnelDataMsg (uint32_t tunnelID, const uint8_t * payload)
	{
		auto msg = NewI2NPTunnelMessage ();
		htobe32buf (msg->GetPayload (), tunnelID);
		msg->len += 4; // tunnelID
		msg->Concat (payload, i2p::tunnel::TUNNEL_DATA_MSG_SIZE - 4);
		msg->FillI2NPMessageHeader (eI2NPTunnelData);
		return msg;
	}

	std::shared_ptr<I2NPMessage> CreateEmptyTunnelDataMsg ()
	{
		auto msg = NewI2NPTunnelMessage ();
		msg->len += i2p::tunnel::TUNNEL_DATA_MSG_SIZE;
		return msg;
	}

	std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (uint32_t tunnelID, const uint8_t * buf, size_t len)
	{
		auto msg = NewI2NPMessage (len);
		uint8_t * payload = msg->GetPayload ();
		htobe32buf (payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
		htobe16buf (payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, len);
		msg->len += TUNNEL_GATEWAY_HEADER_SIZE;
		if (msg->Concat (buf, len) < len)
			LogPrint (eLogError, "I2NP: tunnel gateway buffer overflow ", msg->maxLen);
		msg->FillI2NPMessageHeader (eI2NPTunnelGateway);
		return msg;
	}

	std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (uint32_t tunnelID, std::shared_ptr<I2NPMessage> msg)
	{
		if (msg->offset >= I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE)
		{
			// message is capable to be used without copying
			uint8_t * payload = msg->GetBuffer () - TUNNEL_GATEWAY_HEADER_SIZE;
			htobe32buf (payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
			int len = msg->GetLength ();
			htobe16buf (payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, len);
			msg->offset -= (I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE);
			msg->len = msg->offset + I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE +len;
			msg->FillI2NPMessageHeader (eI2NPTunnelGateway);
			return msg;
		}
		else
			return CreateTunnelGatewayMsg (tunnelID, msg->GetBuffer (), msg->GetLength ());
	}

	std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (uint32_t tunnelID, I2NPMessageType msgType,
		const uint8_t * buf, size_t len, uint32_t replyMsgID)
	{
		auto msg = NewI2NPMessage (len);
		size_t gatewayMsgOffset = I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE;
		msg->offset += gatewayMsgOffset;
		msg->len += gatewayMsgOffset;
		if (msg->Concat (buf, len) < len)
			LogPrint (eLogError, "I2NP: tunnel gateway buffer overflow ", msg->maxLen);
		msg->FillI2NPMessageHeader (msgType, replyMsgID); // create content message
		len = msg->GetLength ();
		msg->offset -= gatewayMsgOffset;
		uint8_t * payload = msg->GetPayload ();
		htobe32buf (payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
		htobe16buf (payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, len);
		msg->FillI2NPMessageHeader (eI2NPTunnelGateway); // gateway message
		return msg;
	}

	size_t GetI2NPMessageLength (const uint8_t * msg)
	{
		return bufbe16toh (msg + I2NP_HEADER_SIZE_OFFSET) + I2NP_HEADER_SIZE;
	}

}
