#include "Aligned.h"

namespace i2p
{
namespace client
{

	AlignedDestination::~AlignedDestination() {}
	AlignedDestination::AlignedDestination(const i2p::data::PrivateKeys& keys, bool isPublic, const std::map<std::string, std::string> * params) : ClientDestination(keys, isPublic, params) {}


	void AlignedDestination::HandleGotAlignedRoutingPathForStream(AlignedRoutingSession_ptr session, StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port)
	{
		if(session)
			ClientDestination::CreateStream(streamRequestComplete, dest, port);
		else
			streamRequestComplete(nullptr);
	}

	void AlignedDestination::CreateStream(StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port)
	{
		auto gotLeaseSet = [&](std::shared_ptr<const i2p::data::LeaseSet> ls ) {
			if(!ls)
			{
				streamRequestComplete(nullptr);
				return;
			}
			auto leases = ls->GetNonExpiredLeases ();
			if(leases.size())
			{
				auto lease = leases[rand() % leases.size()];
				ObtainAlignedRoutingPath(ls, lease->tunnelGateway, true, std::bind(&AlignedDestination::HandleGotAlignedRoutingPathForStream, this, std::placeholders::_1, streamRequestComplete, dest, port));
			}
			else
				ClientDestination::CreateStream(streamRequestComplete, dest, port);
		};
		auto ls = FindLeaseSet(dest);
		if(ls)
		{
			gotLeaseSet(ls);
		}
		else
		{
			RequestDestination(dest, gotLeaseSet);
		}
	}

	std::shared_ptr<i2p::stream::Stream> AlignedDestination::CreateStream(std::shared_ptr<const i2p::data::LeaseSet> remote, int port)
	{
		auto leases = remote->GetNonExpiredLeases();
		if(leases.size())
		{
			auto lease = leases[rand() % leases.size()];
			PrepareOutboundTunnelTo(lease->tunnelGateway, remote);
		}
		return ClientDestination::CreateStream(remote, port);
	}

	IOutboundTunnelSelector::OBTunnel_ptr AlignedDestination::GetAlignedTunnelTo(const RemoteDestination_t & gateway)
	{
		OBTunnel_ptr tun = nullptr;
		VisitAllRoutingSessions([&tun, gateway](i2p::garlic::GarlicRoutingSessionPtr s) {
			if(tun) return;
			s->VisitSharedRoutingPath([&tun, gateway](std::shared_ptr<i2p::garlic::GarlicRoutingPath> p) {
				if(p && p->remoteLease && p->remoteLease->tunnelGateway == gateway)
					tun = p->outboundTunnel;
			});
		});
		return tun;
	}

	IOutboundTunnelSelector::OBTunnel_ptr AlignedDestination::GetOutboundTunnelFor(const RemoteDestination_t & destination, OBTunnel_ptr excluding)
	{
		auto ls = FindLeaseSet(destination);
		if(ls)
		{
			auto session = GetRoutingSession(ls, true);
			if(session)
			{
				auto path = session->GetSharedRoutingPath();
				if(path && path->outboundTunnel)
					return path->outboundTunnel;
			}
		}
		return GetTunnelPool()->GetNextOutboundTunnel(excluding);
	}

	bool AlignedDestination::HasOutboundTunnelTo(const i2p::data::IdentHash & gateway)
	{
		bool found = false;
		VisitAllRoutingSessions([&found, gateway] (i2p::garlic::GarlicRoutingSessionPtr s) {
				if(found) return;
				s->VisitSharedRoutingPath([&found, gateway](std::shared_ptr<i2p::garlic::GarlicRoutingPath> p) {
						found = p && p->outboundTunnel && p->outboundTunnel->GetEndpointIdentHash() == gateway;
				});
		});
		return found;
	}

	void AlignedDestination::PrepareOutboundTunnelTo(const RemoteDestination_t & gateway, RoutingDestination_ptr remote)
	{
		ObtainAlignedRoutingPath(remote, gateway, false, [](AlignedRoutingSession_ptr) {});
	}

	i2p::garlic::GarlicRoutingSessionPtr AlignedDestination::CreateNewRoutingSession(std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLS)
	{
		return std::make_shared<AlignedRoutingSession>(this, destination, numTags, attachLS);
	}

	void AlignedDestination::ObtainAlignedRoutingPath(RoutingDestination_ptr destination, const RemoteDestination_t & gateway, bool attachLS, AlignedPathObtainedFunc obtained)
	{
		auto ls = FindLeaseSet(destination->GetIdentHash());
		auto leases = ls->GetNonExpiredLeasesExcluding([gateway](const i2p::data::Lease l) -> bool {
			return gateway != l.tunnelGateway;
		});
		if(leases.size())
		{
			auto session = GetRoutingSession(destination, attachLS);
			std::shared_ptr<AlignedRoutingSession> s(static_cast<AlignedRoutingSession *>(session.get()));
			s->SetCurrentLease(leases[rand() % leases.size()]);
			s->SetBuildCompleteCallback(std::bind(obtained, s));
		}
	}


	AlignedRoutingSession::AlignedRoutingSession(AlignedDestination * owner, std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLeaseSet) :
		i2p::garlic::GarlicRoutingSession(owner, destination, numTags, attachLeaseSet),
		m_BuildCompleted(nullptr)
	{
		auto pool = owner->GetTunnelPool();
		m_AlignedPool = i2p::tunnel::tunnels.CreateTunnelPool(pool->GetNumInboundHops(), pool->GetNumOutboundHops(), 1, 2);
		m_AlignedPool->SetCustomPeerSelector(this);
	}

	AlignedRoutingSession::~AlignedRoutingSession()
	{
		i2p::tunnel::tunnels.DeleteTunnelPool(m_AlignedPool);
	}

	bool AlignedRoutingSession::OnBuildResult(const i2p::tunnel::Path & path, bool inbound, i2p::tunnel::TunnelBuildResult result)
	{
		if(!inbound && result == i2p::tunnel::eBuildResultOkay)
		{
			auto obep = path[path.size() - 1]->GetIdentHash();
			if(m_CurrentRemoteLease && obep == m_CurrentRemoteLease->tunnelGateway && m_BuildCompleted)
			{
				// matches our build
				m_BuildCompleted();
				m_BuildCompleted = nullptr;
			}
		}
		return true;
	}

	bool AlignedRoutingSession::SelectPeers(i2p::tunnel::Path & path, int hops, bool inbound)
	{
		auto selectNextHop = std::bind(&i2p::tunnel::TunnelPool::SelectNextHop, m_AlignedPool, std::placeholders::_1);
		if(!i2p::tunnel::StandardSelectPeers(path, hops, inbound, &i2p::tunnel::StandardSelectFirstHop, selectNextHop))
			return false;
		std::shared_ptr<i2p::data::RouterInfo> obep = nullptr;
		if(!inbound && m_CurrentRemoteLease)
		{
			obep = i2p::data::netdb.FindRouter(m_CurrentRemoteLease->tunnelGateway);
			if(obep)
				path.push_back(obep->GetRouterIdentity());
		}
		return inbound || obep;
	}
	std::shared_ptr<i2p::garlic::GarlicRoutingPath> AlignedRoutingSession::GetSharedRoutingPath()
	{
		if(HasSharedRoutingPath())
			return i2p::garlic::GarlicRoutingSession::GetSharedRoutingPath();
		size_t obHops = m_AlignedPool->GetNumOutboundHops();
		auto path = std::make_shared<i2p::garlic::GarlicRoutingPath>();
		auto tuns = m_AlignedPool->GetOutboundTunnelsWhere([obHops](std::shared_ptr<i2p::tunnel::OutboundTunnel> tun) -> bool {
				return tun && tun->GetPeers().size() > obHops;
		});
		uint64_t minLatency = 900000;
		for(auto & tun : tuns)
		{
			auto l = tun->GetMeanLatency();
			if(tun->LatencyIsKnown() && l < minLatency)
			{
				minLatency = l;
				path->outboundTunnel = tun;
			}
		}
		path->remoteLease = m_CurrentRemoteLease;
		return path;
	}

}
}
