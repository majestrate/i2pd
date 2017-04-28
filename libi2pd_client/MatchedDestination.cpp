#include "MatchedDestination.h"
#include "Log.h"
#include "ClientContext.h"


namespace i2p
{
namespace client
{
	MatchedTunnelDestination::MatchedTunnelDestination(const i2p::data::PrivateKeys & keys, const std::string & remoteName, const std::map<std::string, std::string> * params)
		: ClientDestination(keys, false, params),
			m_RemoteName(remoteName), m_HasPath(false) {}


	void MatchedTunnelDestination::ResolveCurrentLeaseSet()
	{
		if(i2p::client::context.GetAddressBook().GetIdentHash(m_RemoteName, m_RemoteIdent))
		{
			auto ls = FindLeaseSet(m_RemoteIdent);
			if(ls)
			{
				HandleFoundCurrentLeaseSet(ls);
			}
			else
				RequestDestination(m_RemoteIdent, std::bind(&MatchedTunnelDestination::HandleFoundCurrentLeaseSet, this, std::placeholders::_1));
		}
		else
			LogPrint(eLogWarning, "Destination: failed to resolve ", m_RemoteName);
	}

	void MatchedTunnelDestination::HandleFoundCurrentLeaseSet(std::shared_ptr<const i2p::data::LeaseSet> ls)
	{
		if(ls)
		{
			LogPrint(eLogDebug, "Destination: resolved remote lease set for ", m_RemoteName);
			m_RemoteLeaseSet = ls;
		}
		else
		{
			m_ResolveTimer->expires_from_now(boost::posix_time::seconds(1));
			m_ResolveTimer->async_wait([&](const boost::system::error_code & ec) {
					if(!ec)	ResolveCurrentLeaseSet();
			});
		}
	}

	void MatchedTunnelDestination::CreateStream (StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash& dest, int port)
	{
			if(OutboundTunnelsAreReady())
				ClientDestination::CreateStream(streamRequestComplete, dest, port);
			else
				streamRequestComplete(nullptr);
	}

	bool MatchedTunnelDestination::Start()
	{
		if(ClientDestination::Start())
		{
			m_ResolveTimer = std::make_shared<boost::asio::deadline_timer>(GetService());
			GetTunnelPool()->SetCustomPeerSelector(this);
			ResolveCurrentLeaseSet();
			return true;
		}
		else
			return false;
	}

	bool MatchedTunnelDestination::Stop()
	{
		if(ClientDestination::Stop())
		{
			if(m_ResolveTimer)
				m_ResolveTimer->cancel();
			return true;
		}
		else
			return false;
	}


	bool MatchedTunnelDestination::SelectPeers(i2p::tunnel::Path & path, int hops, bool inbound)
	{
		auto pool = GetTunnelPool();
		if(!i2p::tunnel::StandardSelectPeers(path, hops, inbound, std::bind(&i2p::tunnel::TunnelPool::SelectNextHop, pool, std::placeholders::_1)))
			return false;
		// more here for outbound tunnels
		if(!inbound && m_RemoteLeaseSet)
		{
			if(m_RemoteLeaseSet->IsExpired())
			{
				ResolveCurrentLeaseSet();
			}
			if(m_RemoteLeaseSet && !m_RemoteLeaseSet->IsExpired())
			{
				// remote lease set is good
				auto leases = m_RemoteLeaseSet->GetNonExpiredLeases();
				// pick lease
				std::shared_ptr<i2p::data::RouterInfo> obep;
				while(!obep && leases.size() > 0) {
					auto idx = rand() % leases.size();
					auto lease = leases[idx];
					obep = i2p::data::netdb.FindRouter(lease->tunnelGateway);
					leases.erase(leases.begin()+idx);
				}
				if(obep) {
					path.push_back(obep->GetRouterIdentity());
					LogPrint(eLogDebug, "Destination: found OBEP matching IBGW");
				} else
					LogPrint(eLogWarning, "Destination: could not find proper IBGW for matched outbound tunnel");
			}
		}
		return true;
	}

	bool MatchedTunnelDestination::OnBuildResult(const i2p::tunnel::Path & path, bool inbound, i2p::tunnel::TunnelBuildResult result)
	{
		if(!inbound && result == i2p::tunnel::eBuildResultOkay)
		{
			auto pool = GetTunnelPool();
			auto sz = path.size();
			if(pool->GetOutboundTunnelLength() < sz)
			{
				// this is a matched tunnel
				m_HasPath = true;
			}
		}
		return true;
	}


	std::shared_ptr<i2p::tunnel::OutboundTunnel> MatchedTunnelDestination::GetNewOutboundTunnelForLease(const std::shared_ptr<i2p::tunnel::OutboundTunnel> & excluding, const i2p::data::IdentHash & remoteLease)
	{
		std::shared_ptr<i2p::tunnel::OutboundTunnel> found = nullptr;
		auto pool = GetTunnelPool();
		size_t tunlen = pool->GetOutboundTunnelLength();
		std::vector<std::shared_ptr<i2p::tunnel::OutboundTunnel> > tuns;
		tuns = pool->GetOutboundTunnelsMatching([tunlen] (const std::shared_ptr<i2p::tunnel::OutboundTunnel> &tun) -> bool {
				return tun->GetLength() > tunlen;
		});
		// get lowest latency
		uint64_t minLatency = 100000000;
		for (const auto & itr : tuns)
		{
			if(!itr->LatencyIsKnown()) continue;

			uint64_t latency = itr->GetMeanLatency() ;
			if (latency < minLatency)
			{
				found = itr;
				minLatency = latency;
			}
		}
		return found;
	}
}
}
