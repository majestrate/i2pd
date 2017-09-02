#include "I2PRouter.h"
#include "Log.h"
#include "Config.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "Transports.h"

namespace i2p
{

  const uint16_t DEFAULT_MAX_NUM_TRANSIT_TUNNELS = 2500;

	struct RouterImpl
	{
		i2p::data::NetDb netdb;
		i2p::tunnel::Tunnels tunnels;
		i2p::transport::Transports transports;
		i2p::RouterContext context;
	};

	Router::Router() :
		impl(new RouterImpl),
		m_MaxTransitTunnels(DEFAULT_MAX_NUM_TRANSIT_TUNNELS)
	{
	}

	Router::~Router()
	{
		delete impl;
	}

	bool Router::Init()
	{

			bool precomputation; i2p::config::GetOption("precomputation.elgamal", precomputation);
			i2p::crypto::InitCrypto (precomputation);

			int netID; i2p::config::GetOption("netid", netID);
		  impl->context.SetNetID (netID);
			impl->context.Init ();

			bool ipv6;		i2p::config::GetOption("ipv6", ipv6);
			bool ipv4;		i2p::config::GetOption("ipv4", ipv4);
#ifdef MESHNET
			// manual override for meshnet
			ipv4 = false;
			ipv6 = true;
#endif
			uint16_t port; i2p::config::GetOption("port", port);
			if (!i2p::config::IsDefault("port"))
			{
				LogPrint(eLogInfo, "Daemon: accepting incoming connections at port ", port);
			  impl->context.UpdatePort (port);
			}
			impl->context.SetSupportsV6		 (ipv6);
		  impl->context.SetSupportsV4		 (ipv4);

			bool transit; i2p::config::GetOption("notransit", transit);
		  impl->context.SetAcceptsTunnels (!transit);
			uint16_t transitTunnels; i2p::config::GetOption("limits.transittunnels", transitTunnels);
			SetMaxNumTransitTunnels (transitTunnels);

			bool isFloodfill; i2p::config::GetOption("floodfill", isFloodfill);
			if (isFloodfill) {
				LogPrint(eLogInfo, "Daemon: router will be floodfill");
			  impl->context.SetFloodfill (true);
			}	else {
			  impl->context.SetFloodfill (false);
			}

			/* this section also honors 'floodfill' flag, if set above */
			std::string bandwidth; i2p::config::GetOption("bandwidth", bandwidth);
			if (bandwidth.length () > 0)
			{
				if (bandwidth[0] >= 'K' && bandwidth[0] <= 'X')
				{
					impl->context.SetBandwidth (bandwidth[0]);
					LogPrint(eLogInfo, "Daemon: bandwidth set to ", impl->context.GetBandwidthLimit (), "KBps");
				}
				else
				{
					auto value = std::atoi(bandwidth.c_str());
					if (value > 0)
					{
					  impl->context.SetBandwidth (value);
						LogPrint(eLogInfo, "Daemon: bandwidth set to ",impl->context.GetBandwidthLimit (), " KBps");
					}
					else
					{
						LogPrint(eLogInfo, "Daemon: unexpected bandwidth ", bandwidth, ". Set to 'low'");
						impl->context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
					}
				}
			}
			else if (isFloodfill)
			{
				LogPrint(eLogInfo, "Daemon: floodfill bandwidth set to 'extra'");
				impl->context.SetBandwidth (i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH1);
			}
			else
			{
				LogPrint(eLogInfo, "Daemon: bandwidth set to 'low'");
				impl->context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
			}

			int shareRatio; i2p::config::GetOption("share", shareRatio);
			impl->context.SetShareRatio (shareRatio);

			std::string family; i2p::config::GetOption("family", family);
			impl->context.SetFamily (family);
			if (family.length () > 0)
				LogPrint(eLogInfo, "Daemon: family set to ", family);

      bool trust; i2p::config::GetOption("trust.enabled", trust);
      if (trust)
      {
        LogPrint(eLogInfo, "Daemon: explicit trust enabled");
        std::string fam; i2p::config::GetOption("trust.family", fam);
				std::string routers; i2p::config::GetOption("trust.routers", routers);
				bool restricted = false;
        if (fam.length() > 0)
        {
					std::set<std::string> fams;
					size_t pos = 0, comma;
					do
					{
						comma = fam.find (',', pos);
						fams.insert (fam.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
						pos = comma + 1;
					}
					while (comma != std::string::npos);
				  impl->transports.RestrictRoutesToFamilies(fams);
					restricted  = fams.size() > 0;
        }
				if (routers.length() > 0) {
					std::set<i2p::data::IdentHash> idents;
					size_t pos = 0, comma;
					do
					{
						comma = routers.find (',', pos);
						i2p::data::IdentHash ident;
						ident.FromBase64 (routers.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
						idents.insert (ident);
						pos = comma + 1;
					}
					while (comma != std::string::npos);
					LogPrint(eLogInfo, "Daemon: setting restricted routes to use ", idents.size(), " trusted routesrs");
				  impl->transports.RestrictRoutesToRouters(idents);
					restricted = idents.size() > 0;
				}
				if(!restricted)
					LogPrint(eLogError, "Daemon: no trusted routers of families specififed");
      }
      bool hidden; i2p::config::GetOption("trust.hidden", hidden);
      if (hidden)
      {
        LogPrint(eLogInfo, "Daemon: using hidden mode");
        impl->netdb.SetHidden(true);
      }
			return true;
	}


	void Router::HandleI2NPMessage (uint8_t * msg, size_t len)
	{
		uint8_t typeID = msg[I2NP_HEADER_TYPEID_OFFSET];
		uint32_t msgID = bufbe32toh (msg + I2NP_HEADER_MSGID_OFFSET);
		LogPrint (eLogDebug, "I2NP: msg received len=", len,", type=", (int)typeID, ", msgID=", (unsigned int)msgID);
		uint8_t * buf = msg + I2NP_HEADER_SIZE;
		int size = bufbe16toh (msg + I2NP_HEADER_SIZE_OFFSET);
		switch (typeID)
		{
			case eI2NPVariableTunnelBuild:
				HandleVariableTunnelBuildMsg  (msgID, buf, size);
			break;
			case eI2NPVariableTunnelBuildReply:
				HandleVariableTunnelBuildReplyMsg (msgID, buf, size);
			break;
			case eI2NPTunnelBuild:
				HandleTunnelBuildMsg  (buf, size);
			break;
			case eI2NPTunnelBuildReply:
				// TODO:
			break;
			default:
				LogPrint (eLogWarning, "I2NP: Unexpected message ", (int)typeID);
		}
	}

	void Router::PostTunnelData(std::vector<std::shared_ptr<I2NPMessage> > & msgs)
	{
		impl->tunnels.PostTunnelData(msgs);
	}

	void Router::HandleI2NPMessage (std::shared_ptr<I2NPMessage> msg)
	{
		if (msg)
		{
			uint8_t typeID = msg->GetTypeID ();
			LogPrint (eLogDebug, "I2NP: Handling message with type ", (int)typeID);
			switch (typeID)
			{
				case eI2NPTunnelData:
				  impl->tunnels.PostTunnelData (msg);
				break;
				case eI2NPTunnelGateway:
					impl->tunnels.PostTunnelData (msg);
				break;
				case eI2NPGarlic:
				{
					if (msg->from)
					{
						if (msg->from->GetTunnelPool ())
							msg->from->GetTunnelPool ()->ProcessGarlicMessage (msg);
						else
							LogPrint (eLogInfo, "I2NP: Local destination for garlic doesn't exist anymore");
					}
					else
					  impl->context.ProcessGarlicMessage (msg);
					break;
				}
				case eI2NPDatabaseStore:
				case eI2NPDatabaseSearchReply:
				case eI2NPDatabaseLookup:
					// forward to netDb
					impl->netdb.PostI2NPMsg(msg);
				break;
				case eI2NPDeliveryStatus:
				{
					if (msg->from && msg->from->GetTunnelPool ())
						msg->from->GetTunnelPool ()->ProcessDeliveryStatus (msg);
					else
						impl->context.ProcessDeliveryStatusMessage (msg);
					break;
				}
				case eI2NPVariableTunnelBuild:
				case eI2NPVariableTunnelBuildReply:
				case eI2NPTunnelBuild:
				case eI2NPTunnelBuildReply:
					// forward to tunnel thread
				  impl->tunnels.PostTunnelData (msg);
				break;
				default:
					HandleI2NPMessage (msg->GetBuffer (), msg->GetLength ());
			}
		}
	}

	std::shared_ptr<I2NPMessage> Router::CreateDeliveryStatusMsg (uint32_t msgID)
	{
		auto m = NewI2NPShortMessage ();
		uint8_t * buf = m->GetPayload ();
		if (msgID)
		{
			htobe32buf (buf + DELIVERY_STATUS_MSGID_OFFSET, msgID);
			htobe64buf (buf + DELIVERY_STATUS_TIMESTAMP_OFFSET, i2p::util::GetMillisecondsSinceEpoch ());
		}
		else // for SSU establishment
		{
			RAND_bytes ((uint8_t *)&msgID, 4);
			htobe32buf (buf + DELIVERY_STATUS_MSGID_OFFSET, msgID);
			htobe64buf (buf + DELIVERY_STATUS_TIMESTAMP_OFFSET, impl->context.GetNetID ());
		}
		m->len += DELIVERY_STATUS_SIZE;
		m->FillI2NPMessageHeader (eI2NPDeliveryStatus);
		return m;
	}


	std::shared_ptr<I2NPMessage> Router::CreateDatabaseSearchReply (const i2p::data::IdentHash& ident,
																																			std::vector<i2p::data::IdentHash> routers)
	{
		auto m = NewI2NPShortMessage ();
		uint8_t * buf = m->GetPayload ();
		size_t len = 0;
		memcpy (buf, ident, 32);
		len += 32;
		buf[len] = routers.size ();
		len++;
		for (const auto& it: routers)
			{
				memcpy (buf + len, it, 32);
				len += 32;
			}
		memcpy (buf + len, impl->context.GetRouterInfo ().GetIdentHash (), 32);
		len += 32;
		m->len += len;
		m->FillI2NPMessageHeader (eI2NPDatabaseSearchReply);
		return m;
	}

	bool Router::HandleBuildRequestRecords (int num, uint8_t * records, uint8_t * clearText)
	{
		for (int i = 0; i < num; i++)
		{
			uint8_t * record = records + i*TUNNEL_BUILD_RECORD_SIZE;
			if (!memcmp (record + BUILD_REQUEST_RECORD_TO_PEER_OFFSET, (const uint8_t *)impl->context.GetRouterInfo ().GetIdentHash (), 16))
			{
				LogPrint (eLogDebug, "I2NP: Build request record ", i, " is ours");
				BN_CTX * ctx = BN_CTX_new ();
				i2p::crypto::ElGamalDecrypt (impl->context.GetEncryptionPrivateKey (), record + BUILD_REQUEST_RECORD_ENCRYPTED_OFFSET, clearText, ctx);
				BN_CTX_free (ctx);
				// replace record to reply
				if (impl->context.AcceptsTunnels () &&
					impl->tunnels.GetTransitTunnels ().size () <= m_MaxTransitTunnels &&
					!impl->transports.IsBandwidthExceeded () &&
					!impl->transports.IsTransitBandwidthExceeded ())
				{
					auto transitTunnel = i2p::tunnel::CreateTransitTunnel (
							bufbe32toh (clearText + BUILD_REQUEST_RECORD_RECEIVE_TUNNEL_OFFSET),
							clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
						    bufbe32toh (clearText + BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET),
							clearText + BUILD_REQUEST_RECORD_LAYER_KEY_OFFSET,
						    clearText + BUILD_REQUEST_RECORD_IV_KEY_OFFSET,
							clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET] & 0x80,
						    clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET ] & 0x40);
				  impl->tunnels.AddTransitTunnel (transitTunnel);
					record[BUILD_RESPONSE_RECORD_RET_OFFSET] = 0;
				}
				else
					record[BUILD_RESPONSE_RECORD_RET_OFFSET] = 30; // always reject with bandwidth reason (30)

				//TODO: fill filler
				SHA256 (record + BUILD_RESPONSE_RECORD_PADDING_OFFSET, BUILD_RESPONSE_RECORD_PADDING_SIZE + 1, // + 1 byte of ret
					record + BUILD_RESPONSE_RECORD_HASH_OFFSET);
				// encrypt reply
				i2p::crypto::CBCEncryption encryption;
				for (int j = 0; j < num; j++)
				{
					encryption.SetKey (clearText + BUILD_REQUEST_RECORD_REPLY_KEY_OFFSET);
					encryption.SetIV (clearText + BUILD_REQUEST_RECORD_REPLY_IV_OFFSET);
					uint8_t * reply = records + j*TUNNEL_BUILD_RECORD_SIZE;
					encryption.Encrypt(reply, TUNNEL_BUILD_RECORD_SIZE, reply);
				}
				return true;
			}
		}
		return false;
	}



	void Router::HandleVariableTunnelBuildMsg (uint32_t replyMsgID, uint8_t * buf, size_t len)
	{
		int num = buf[0];
		LogPrint (eLogDebug, "I2NP: VariableTunnelBuild ", num, " records");
		if (len < num*BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE + 1)
		{
			LogPrint (eLogError, "VaribleTunnelBuild message of ", num, " records is too short ", len);
			return;
		}

		auto tunnel = impl->tunnels.GetPendingInboundTunnel (replyMsgID);
		if (tunnel)
		{
			// endpoint of inbound tunnel
			LogPrint (eLogDebug, "I2NP: VariableTunnelBuild reply for tunnel ", tunnel->GetTunnelID ());
			if (tunnel->HandleTunnelBuildResponse (buf, len))
			{
				LogPrint (eLogInfo, "I2NP: Inbound tunnel ", tunnel->GetTunnelID (), " has been created");
				tunnel->SetState (i2p::tunnel::eTunnelStateEstablished);
				impl->tunnels.AddInboundTunnel (tunnel);
			}
			else
			{
				LogPrint (eLogInfo, "I2NP: Inbound tunnel ", tunnel->GetTunnelID (), " has been declined");
				tunnel->SetState (i2p::tunnel::eTunnelStateBuildFailed);
			}
		}
		else
		{
			uint8_t clearText[BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE];
			if (HandleBuildRequestRecords (num, buf + 1, clearText))
			{
				if (clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET] & 0x40) // we are endpoint of outboud tunnel
				{
					// so we send it to reply tunnel
					impl->transports.SendMessage (clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
						CreateTunnelGatewayMsg (bufbe32toh (clearText + BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET),
							eI2NPVariableTunnelBuildReply, buf, len,
						    bufbe32toh (clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET)));
				}
				else
					impl->transports.SendMessage (clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
						CreateI2NPMessage (eI2NPVariableTunnelBuild, buf, len,
							bufbe32toh (clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET)));
			}
		}
	}

	void Router::HandleTunnelBuildMsg (uint8_t * buf, size_t len)
	{
		if (len < NUM_TUNNEL_BUILD_RECORDS*BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE)
		{
			LogPrint (eLogError, "TunnelBuild message is too short ", len);
			return;
		}
		uint8_t clearText[BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE];
		if (HandleBuildRequestRecords (NUM_TUNNEL_BUILD_RECORDS, buf, clearText))
		{
			if (clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET] & 0x40) // we are endpoint of outbound tunnel
			{
				// so we send it to reply tunnel
				impl->transports.SendMessage (clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
					CreateTunnelGatewayMsg (bufbe32toh (clearText + BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET),
						eI2NPTunnelBuildReply, buf, len,
					    bufbe32toh (clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET)));
			}
			else
				impl->transports.SendMessage (clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
					CreateI2NPMessage (eI2NPTunnelBuild, buf, len,
						bufbe32toh (clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET)));
		}
	}

	void Router::HandleVariableTunnelBuildReplyMsg (uint32_t replyMsgID, uint8_t * buf, size_t len)
	{
		int num = buf[0];
		LogPrint (eLogDebug, "I2NP: VariableTunnelBuildReplyMsg of ", num, " records replyMsgID=", replyMsgID);
		if (len < num*BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE + 1)
		{
			LogPrint (eLogError, "VaribleTunnelBuildReply message of ", num, " records is too short ", len);
			return;
		}

		auto tunnel = impl->tunnels.GetPendingOutboundTunnel (replyMsgID);
		if (tunnel)
		{
			// reply for outbound tunnel
			if (tunnel->HandleTunnelBuildResponse (buf, len))
			{
				LogPrint (eLogInfo, "I2NP: Outbound tunnel ", tunnel->GetTunnelID (), " has been created");
				tunnel->SetState (i2p::tunnel::eTunnelStateEstablished);
				impl->tunnels.AddOutboundTunnel (tunnel);
			}
			else
			{
				LogPrint (eLogInfo, "I2NP: Outbound tunnel ", tunnel->GetTunnelID (), " has been declined");
				tunnel->SetState (i2p::tunnel::eTunnelStateBuildFailed);
			}
		}
		else
			LogPrint (eLogWarning, "I2NP: Pending tunnel for message ", replyMsgID, " not found");
	}


}
