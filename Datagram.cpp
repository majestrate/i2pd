#include <string.h>
#include <vector>
#include "Crypto.h"
#include "Log.h"
#include "TunnelBase.h"
#include "RouterContext.h"
#include "Destination.h"
#include "Datagram.h"

namespace i2p
{
namespace datagram
{
	DatagramDestination::DatagramDestination (std::shared_ptr<i2p::client::ClientDestination> owner): 
		m_Owner (owner.get()),
		m_Receiver (nullptr)
	{
		m_Identity.FromBase64 (owner->GetIdentity()->ToBase64());
	}
	
	DatagramDestination::~DatagramDestination ()
	{
		m_Sessions.clear();
	}
	
	void DatagramDestination::SendDatagramTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash& ident, uint16_t fromPort, uint16_t toPort)
	{
		auto owner = m_Owner;
		std::vector<uint8_t> v(MAX_DATAGRAM_SIZE);
		uint8_t * buf = v.data();
		auto identityLen = m_Identity.ToBuffer (buf, MAX_DATAGRAM_SIZE);
		uint8_t * signature = buf + identityLen;
		auto signatureLen = m_Identity.GetSignatureLen ();
		uint8_t * buf1 = signature + signatureLen;
		size_t headerLen = identityLen + signatureLen;
		
		memcpy (buf1, payload, len);	
		if (m_Identity.GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_DSA_SHA1)
		{
			uint8_t hash[32];	
			SHA256(buf1, len, hash);
			owner->Sign (hash, 32, signature);
		}
		else
			owner->Sign (buf1, len, signature);

		auto msg = CreateDataMessage (buf, len + headerLen, fromPort, toPort);
		auto session = ObtainSession(ident);
		session->SendMsg(msg);
	}


	void DatagramDestination::HandleDatagram (uint16_t fromPort, uint16_t toPort,uint8_t * const &buf, size_t len)
	{
		i2p::data::IdentityEx identity;
		size_t identityLen = identity.FromBuffer (buf, len);
		const uint8_t * signature = buf + identityLen;
		size_t headerLen = identityLen + identity.GetSignatureLen ();

		bool verified = false;
		if (identity.GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_DSA_SHA1)
		{
			uint8_t hash[32];
			SHA256(buf + headerLen, len - headerLen, hash);
			verified = identity.Verify (hash, 32, signature);
		}	
		else	
			verified = identity.Verify (buf + headerLen, len - headerLen, signature);
				
		if (verified)
		{
			auto r = FindReceiver(toPort);
			if(r)
				r(identity, fromPort, toPort, buf + headerLen, len -headerLen);
			else
				LogPrint (eLogWarning, "DatagramDestination: no receiver for port ", toPort);
		}
		else
			LogPrint (eLogWarning, "Datagram signature verification failed");	
	}

	DatagramDestination::Receiver DatagramDestination::FindReceiver(uint16_t port)
	{
		std::lock_guard<std::mutex> lock(m_ReceiversMutex);
		Receiver r = m_Receiver;
		auto itr = m_ReceiversByPorts.find(port);
		if (itr != m_ReceiversByPorts.end())
			r = itr->second;
		return r;
	}

	void DatagramDestination::HandleDataMessagePayload (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		// unzip it
		uint8_t uncompressed[MAX_DATAGRAM_SIZE];
		size_t uncompressedLen = m_Inflator.Inflate (buf, len, uncompressed, MAX_DATAGRAM_SIZE);
		if (uncompressedLen)
			HandleDatagram (fromPort, toPort, uncompressed, uncompressedLen);
	}

	std::shared_ptr<I2NPMessage> DatagramDestination::CreateDataMessage (const uint8_t * payload, size_t len, uint16_t fromPort, uint16_t toPort)
	{
		auto msg = NewI2NPMessage ();
		uint8_t * buf = msg->GetPayload ();
		buf += 4; // reserve for length
		size_t size = m_Deflator.Deflate (payload, len, buf, msg->maxLen - msg->len);
		if (size)
		{
			htobe32buf (msg->GetPayload (), size); // length
			htobe16buf (buf + 4, fromPort); // source port
			htobe16buf (buf + 6, toPort); // destination port 
			buf[9] = i2p::client::PROTOCOL_TYPE_DATAGRAM; // datagram protocol
			msg->len += size + 4; 
			msg->FillI2NPMessageHeader (eI2NPData);
		}	
		else
			msg = nullptr;
		return msg;
	}

	void DatagramDestination::CleanUp ()
	{			
		if (m_Sessions.empty ()) return;
		auto now = i2p::util::GetMillisecondsSinceEpoch();
		LogPrint(eLogDebug, "DatagramDestination: clean up sessions");
		std::unique_lock<std::mutex> lock(m_SessionsMutex);
		// for each session ...
		for (auto it = m_Sessions.begin (); it != m_Sessions.end (); ) 
		{
			// check if expired
			if (now - it->second->LastActivity() >= DATAGRAM_SESSION_MAX_IDLE)
			{
				LogPrint(eLogInfo, "DatagramDestination: expiring idle session with ", it->first.ToBase32());
				it = m_Sessions.erase (it); // we are expired
			}
			else
				it++;
		}
	}
	
	std::shared_ptr<DatagramSession> DatagramDestination::ObtainSession(const i2p::data::IdentHash & ident)
	{
		std::shared_ptr<DatagramSession> session = nullptr;
		std::lock_guard<std::mutex> lock(m_SessionsMutex);
		auto itr = m_Sessions.find(ident);
		if (itr == m_Sessions.end()) {
			// not found, create new session
			session = std::make_shared<DatagramSession>(m_Owner, ident);
			m_Sessions[ident] = session;
		} else {
			session = itr->second;
		}
		return session;
	}

	std::shared_ptr<DatagramSession::Info> DatagramDestination::GetInfoForRemote(const i2p::data::IdentHash & remote)
	{
		std::lock_guard<std::mutex> lock(m_SessionsMutex);
		for ( auto & item : m_Sessions)
		{
			if(item.first == remote) return std::make_shared<DatagramSession::Info>(item.second->GetSessionInfo());
		}
		return nullptr;
	}
	
	DatagramSession::DatagramSession(i2p::client::ClientDestination * localDestination,
		const i2p::data::IdentHash & remoteIdent) :
		m_LocalDestination(localDestination),
		m_RemoteIdentity(remoteIdent),
		m_LastUse(i2p::util::GetMillisecondsSinceEpoch ()),
		m_LastPathChange(0),
		m_LastSuccess(0)
	{
	}

	void DatagramSession::SendMsg(std::shared_ptr<I2NPMessage> msg)
	{
		// we used this session
		m_LastUse = i2p::util::GetMillisecondsSinceEpoch();
		// schedule send
		m_LocalDestination->GetService().post(std::bind(&DatagramSession::HandleSend, this, msg));
	}

	DatagramSession::Info DatagramSession::GetSessionInfo() const
	{
		if(!m_RoutingSession)
			return DatagramSession::Info(nullptr, nullptr, m_LastUse, m_LastSuccess);
		
		auto routingPath = m_RoutingSession->GetSharedRoutingPath();
		if (!routingPath)
			return DatagramSession::Info(nullptr, nullptr, m_LastUse, m_LastSuccess);
		auto lease = routingPath->remoteLease;
		auto tunnel = routingPath->outboundTunnel;
		if(lease)
		{
			if(tunnel)
				return DatagramSession::Info(lease->tunnelGateway, tunnel->GetEndpointIdentHash(), m_LastUse, m_LastSuccess);
			else
				return DatagramSession::Info(lease->tunnelGateway, nullptr, m_LastUse, m_LastSuccess);
		}
		else if(tunnel)
			return DatagramSession::Info(nullptr, tunnel->GetEndpointIdentHash(), m_LastUse, m_LastSuccess);
		else
			return DatagramSession::Info(nullptr, nullptr, m_LastUse, m_LastSuccess);
	}
	
	void DatagramSession::HandleSend(std::shared_ptr<I2NPMessage> msg)
	{
		if(!m_RoutingSession)
		{
			// try to get one
			if(m_RemoteLeaseSet) m_RoutingSession = m_LocalDestination->GetRoutingSession(m_RemoteLeaseSet, true);
		}
		// do we have a routing session?
		if(m_RoutingSession)
		{
			// should we switch paths?
			if(ShouldUpdateRoutingPath ())
			{
				LogPrint(eLogDebug, "DatagramSession: try getting new routing path");
				// try switching paths
				auto path = GetNextRoutingPath();
				if(path)
					UpdateRoutingPath (path);
				else
					ResetRoutingPath();
			}
			auto routingPath = m_RoutingSession->GetSharedRoutingPath ();
			// make sure we have a routing path
			if (routingPath)
			{
				auto outboundTunnel = routingPath->outboundTunnel;
				if (outboundTunnel)
				{
					if(outboundTunnel->IsEstablished())
					{
						m_LastSuccess = i2p::util::GetMillisecondsSinceEpoch ();
						// we have a routing path and routing session and the outbound tunnel we are using is good
						// wrap message with routing session and send down routing path's outbound tunnel wrapped for the IBGW
						auto m = m_RoutingSession->WrapSingleMessage(msg);
						routingPath->outboundTunnel->SendTunnelDataMsg({i2p::tunnel::TunnelMessageBlock{
							i2p::tunnel::eDeliveryTypeTunnel,
							routingPath->remoteLease->tunnelGateway, routingPath->remoteLease->tunnelID,
							m
						}});
						return;
					}
				}
			}
		}
		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		// if this path looks dead reset the routing path since we didn't seem to be able to get a path in time
		if (m_LastPathChange && now - m_LastPathChange >= DATAGRAM_SESSION_PATH_TIMEOUT ) ResetRoutingPath();
		UpdateLeaseSet(msg);
	}

	void DatagramSession::UpdateRoutingPath(const std::shared_ptr<i2p::garlic::GarlicRoutingPath> & path)
	{
		m_LastPathChange = i2p::util::GetMillisecondsSinceEpoch ();
		if(m_RoutingSession == nullptr && m_RemoteLeaseSet)
			m_RoutingSession = m_LocalDestination->GetRoutingSession(m_RemoteLeaseSet, true);
		if(!m_RoutingSession) return;
		m_RoutingSession->SetSharedRoutingPath (path);
	}

	bool DatagramSession::ShouldUpdateRoutingPath() const
	{
		bool dead = m_RoutingSession == nullptr || m_RoutingSession->GetSharedRoutingPath () == nullptr;
		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		// we need to rotate paths becuase the routing path is too old
		// if (now - m_LastPathChange >= DATAGRAM_SESSION_PATH_SWITCH_INTERVAL) return true;
		// too fast switching paths
		if (now - m_LastPathChange < DATAGRAM_SESSION_PATH_MIN_LIFETIME ) return false;
		// our path looks dead so we need to rotate paths
		if (now - m_LastSuccess >= DATAGRAM_SESSION_PATH_TIMEOUT) return !dead;
		// if we have a routing session and routing path we don't need to switch paths
		return dead;
	}


	bool DatagramSession::ShouldSwitchLease() const
	{
		std::shared_ptr<i2p::garlic::GarlicRoutingPath> routingPath = nullptr;
		std::shared_ptr<const i2p::data::Lease> currentLease = nullptr;
		if(m_RoutingSession)
			routingPath = m_RoutingSession->GetSharedRoutingPath ();
		if(routingPath)
			currentLease = routingPath->remoteLease;
		if(currentLease) // if we have a lease return true if it's about to expire otherwise return false
			return currentLease->ExpiresWithin( DATAGRAM_SESSION_LEASE_HANDOVER_WINDOW, DATAGRAM_SESSION_LEASE_HANDOVER_FUDGE );
		// we have no current lease, we should switch
		return currentLease == nullptr;
	}
	
	std::shared_ptr<i2p::garlic::GarlicRoutingPath> DatagramSession::GetNextRoutingPath()
	{
		std::shared_ptr<i2p::tunnel::OutboundTunnel> outboundTunnel = nullptr;
		std::shared_ptr<i2p::garlic::GarlicRoutingPath> routingPath = nullptr;
		// get existing routing path if we have one
		if(m_RoutingSession)
			routingPath = m_RoutingSession->GetSharedRoutingPath();
		// do we have an existing outbound tunnel and routing path?
		if(routingPath && routingPath->outboundTunnel)
		{
			// is the outbound tunnel we are using good?
			if (routingPath->outboundTunnel->IsEstablished())
			{
				// ya so let's stick with it
				outboundTunnel = routingPath->outboundTunnel;
			}
			else
				outboundTunnel = m_LocalDestination->GetTunnelPool()->GetNextOutboundTunnel(routingPath->outboundTunnel); // no so we'll switch outbound tunnels
		}
		// do we have an outbound tunnel that works already ?
		if(!outboundTunnel)
			outboundTunnel = m_LocalDestination->GetTunnelPool()->GetNextOutboundTunnel(); // no, let's get a new outbound tunnel as we probably just started

		if(outboundTunnel)
		{
			std::shared_ptr<const i2p::data::Lease> lease = nullptr;
			// should we switch leases ?
			if (ShouldSwitchLease ())
			{
				// yes, get next available lease
				lease = GetNextLease();
			}
			else if (routingPath)
			{
				if(routingPath->remoteLease)
				{
					if(routingPath->remoteLease->ExpiresWithin(DATAGRAM_SESSION_LEASE_HANDOVER_WINDOW, DATAGRAM_SESSION_LEASE_HANDOVER_FUDGE))
						lease = GetNextLease();
					else
						lease = routingPath->remoteLease;
				}
			}
			else
				lease = GetNextLease();
			if(lease)
			{
				// we have a valid lease to use and an outbound tunnel
				// create new routing path
				uint32_t now = i2p::util::GetSecondsSinceEpoch();
				routingPath = std::make_shared<i2p::garlic::GarlicRoutingPath>(i2p::garlic::GarlicRoutingPath{
					outboundTunnel,
					lease,
					0,
					now,
					0
				});
			}
			else // we don't have a new routing path to give
				routingPath = nullptr;
		}
		return routingPath;
	}

	void DatagramSession::ResetRoutingPath()
	{
		if(m_RoutingSession)
		{
			auto routingPath = m_RoutingSession->GetSharedRoutingPath();
			if(routingPath && routingPath->remoteLease) // we have a remote lease already specified and a routing path
			{
				// get outbound tunnel on this path
				auto outboundTunnel = routingPath->outboundTunnel;
				// is this outbound tunnel there and established 
				if (outboundTunnel && outboundTunnel->IsEstablished())
					m_InvalidIBGW.push_back(routingPath->remoteLease->tunnelGateway); // yes, let's mark remote lease as dead because the outbound tunnel seems fine
			}
			// reset the routing path
			UpdateRoutingPath(nullptr);
		}
	}

	std::shared_ptr<const i2p::data::Lease> DatagramSession::GetNextLease()
	{
		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		std::shared_ptr<const i2p::data::Lease> next = nullptr;
		if(m_RemoteLeaseSet)
		{
			std::vector<i2p::data::IdentHash> exclude;
			for(const auto & ident : m_InvalidIBGW)
				exclude.push_back(ident);
			// find get all leases that are not in our ban list and are not going to expire within our lease set handover window + fudge
			auto leases = m_RemoteLeaseSet->GetNonExpiredLeasesExcluding( [&exclude, now] (const i2p::data::Lease & l) -> bool {
				if(exclude.size())
				{
					auto end = std::end(exclude);
					return std::find_if(exclude.begin(), end, [l, now] ( const i2p::data::IdentHash & ident) -> bool {
							return ident == l.tunnelGateway;
						}) != end;
				}
				else
					return false;
			});
			if(leases.size())
			{
				// pick random valid next lease
				uint32_t idx = rand() % leases.size();
				next = leases[idx];
			}
			else
				LogPrint(eLogWarning, "DatagramDestination: no leases to use");
		}
		return next;
	}
	
	void DatagramSession::UpdateLeaseSet(std::shared_ptr<I2NPMessage> msg)
	{
		LogPrint(eLogInfo, "DatagramSession: updating lease set");
		auto ls = m_LocalDestination->FindLeaseSet(m_RemoteIdentity);
		if(ls)
			HandleGotLeaseSet(ls, msg);
		else
			m_LocalDestination->RequestDestination(m_RemoteIdentity, std::bind(&DatagramSession::HandleGotLeaseSet, this, std::placeholders::_1, msg));
	}

	void DatagramSession::HandleGotLeaseSet(std::shared_ptr<const i2p::data::LeaseSet> remoteIdent, std::shared_ptr<I2NPMessage> msg)
	{
		if(remoteIdent)
		{
			// update routing session
			if(remoteIdent != m_RemoteLeaseSet)
			{
				if(m_RoutingSession)
					m_RoutingSession = nullptr;
				m_RoutingSession = m_LocalDestination->GetRoutingSession(remoteIdent, true);
				// clear invalid IBGW as we have a new lease set
				m_InvalidIBGW.clear();
				m_RemoteLeaseSet = remoteIdent;
			}
			// update routing path
			UpdateRoutingPath(GetNextRoutingPath());
			// send the message that was queued if it was provided
			if(msg)
				HandleSend(msg);
			
		}
	}
}
}

