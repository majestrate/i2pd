#include "Aligned.h"
#include "Log.h"

namespace i2p
{
namespace client
{

	AlignedDestination::~AlignedDestination() {}
	AlignedDestination::AlignedDestination(const i2p::data::PrivateKeys& keys, bool isPublic, const std::map<std::string, std::string> * params) : ClientDestination(keys, isPublic, params)
	{
		LogPrint(eLogDebug, "AlignedDestination: creating ....");
	}


	void AlignedDestination::HandleGotAlignedRoutingPathForStream(AlignedRoutingSession_ptr session, StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port)
	{
		if(session)
			ClientDestination::CreateStream(streamRequestComplete, dest, port);
		else
			streamRequestComplete(nullptr);
	}

	void AlignedDestination::CreateStream(StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port)
	{
		LogPrint(eLogDebug, "AlignedDestination: create stream to ",dest.ToBase32(), ":", port);
		auto gotLeaseSet = [=](std::shared_ptr<const i2p::data::LeaseSet> ls ) {
			if(!ls)
			{
				LogPrint(eLogDebug, "AlignedDestination: No LeaseSet");
				streamRequestComplete(nullptr);
				return;
			}
			auto leases = ls->GetNonExpiredLeases ();
			if(leases.size())
			{
				auto lease = leases[rand() % leases.size()];
				auto gateway = GetIBGWFor(dest, lease);
				ObtainAlignedRoutingPath(ls, gateway, true, [&, streamRequestComplete, dest, port](AlignedRoutingSession_ptr s) {
						HandleGotAlignedRoutingPathForStream(s, streamRequestComplete, dest, port);
				});
			}
			else
				streamRequestComplete(nullptr);
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

	IOutboundTunnelSelector::OBTunnel_ptr AlignedDestination::GetNewOutboundTunnel(OBTunnel_ptr excluding)
	{
		return GetTunnelPool()->GetNextOutboundTunnel(excluding);
	}

	IOutboundTunnelSelector::OBTunnel_ptr AlignedDestination::GetAlignedTunnelTo(const RemoteDestination_t & gateway, OBTunnel_ptr excluding)
	{
		OBTunnel_ptr tun = nullptr;
		VisitAllRoutingSessions([&tun, &excluding, gateway](i2p::garlic::GarlicRoutingSessionPtr s) {
			if(tun) return;
			s->VisitSharedRoutingPath([&tun, &excluding, gateway](std::shared_ptr<i2p::garlic::GarlicRoutingPath> p) {
				if(p && p->remoteLease && p->remoteLease->tunnelGateway == gateway && p->outboundTunnel != excluding)
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
		return nullptr;
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
		ObtainAlignedRoutingPath(remote, gateway, true, [](AlignedRoutingSession_ptr) {});
	}

	i2p::garlic::GarlicRoutingSessionPtr AlignedDestination::CreateNewRoutingSession(std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLS)
	{
		return std::make_shared<AlignedRoutingSession>(this, destination, numTags, attachLS);
	}

	void AlignedDestination::ObtainAlignedRoutingPath(RoutingDestination_ptr destination, const RemoteDestination_t & gateway, bool attachLS, AlignedPathObtainedFunc obtained)
	{
		LogPrint(eLogDebug, "AlignedDestination: obtain routing path to IBGW=", gateway.ToBase64());
		auto ls = FindLeaseSet(destination->GetIdentHash());
		auto leases = ls->GetNonExpiredLeasesExcluding([gateway](const i2p::data::Lease l) -> bool {
			return gateway != l.tunnelGateway;
		});
		if(leases.size())
		{
			auto lease = leases[rand() % leases.size()];
			auto gotRouter = [=](std::shared_ptr<i2p::data::RouterInfo> ri) {
				if(!ri) {
					LogPrint(eLogWarning, "AlignedDestinatino failed to find IBGW");
				}
				auto session = GetRoutingSession(destination, attachLS);
				std::shared_ptr<AlignedRoutingSession> s = nullptr;
				s = std::static_pointer_cast<AlignedRoutingSession>(session);
				s->Start();
				s->SetCurrentLease(lease);
				s->AddBuildCompleteCallback([=](){
						std::shared_ptr<AlignedRoutingSession> asess = std::static_pointer_cast<AlignedRoutingSession>(session);
						obtained(asess);
				});
				
			};
			auto obep = i2p::data::netdb.FindRouter(lease->tunnelGateway);
			if(obep)
				gotRouter(obep);
			else
				i2p::data::netdb.RequestDestination(lease->tunnelGateway, gotRouter);
		}
	}


	AlignedRoutingSession::AlignedRoutingSession(AlignedDestination * owner, std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLeaseSet) :
		i2p::garlic::GarlicRoutingSession(owner, destination, numTags, attachLeaseSet),
		m_AlignedPool(nullptr),
		m_Parent(owner)
	{
	}

	AlignedRoutingSession::~AlignedRoutingSession()
	{
		if(m_AlignedPool)
		{
			m_AlignedPool->SetCustomPeerSelector(nullptr);
			i2p::tunnel::tunnels.DeleteTunnelPool(m_AlignedPool);
		}
	}

	void AlignedRoutingSession::AddBuildCompleteCallback(BuildCompleteCallback buildComplete)
	{
		std::vector<std::shared_ptr<i2p::tunnel::OutboundTunnel> > tuns;
		if(m_AlignedPool)
		{
			tuns = m_AlignedPool->GetOutboundTunnelsWhere([&](std::shared_ptr<i2p::tunnel::OutboundTunnel> tun) -> bool {
					return tun && tun->IsEstablished() && tun->GetEndpointIdentHash() == m_CurrentRemoteLease->tunnelGateway;
				}, false);
		}
		if(!tuns.size())
		{
			{
				std::unique_lock<std::mutex> lock(m_BuildCompletedMutex);
				m_BuildCompleted.push_back(buildComplete);
			}
		}
		else
			buildComplete();
	}

	bool AlignedRoutingSession::OnBuildResult(const i2p::tunnel::Path & path, bool inbound, i2p::tunnel::TunnelBuildResult result)
	{
		if(!inbound && result == i2p::tunnel::eBuildResultOkay)
		{
			auto obep = path[path.size() - 1]->GetIdentHash();
			if(m_CurrentRemoteLease && obep == m_CurrentRemoteLease->tunnelGateway)
			{
				// matches our build
				std::vector<BuildCompleteCallback> calls;
				{
					std::unique_lock<std::mutex> lock(m_BuildCompletedMutex);
					for(auto & cb : m_BuildCompleted)
						calls.push_back(cb);
					m_BuildCompleted.clear();
				}
				for(auto & cb : calls) cb();
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
			if(obep) {
				path.push_back(obep->GetRouterIdentity());
				return true;
			} else {
				return false;
			}
		}
		return true;
	}
	std::shared_ptr<i2p::garlic::GarlicRoutingPath> AlignedRoutingSession::GetSharedRoutingPath()
	{
		if(HasSharedRoutingPath())
			return i2p::garlic::GarlicRoutingSession::GetSharedRoutingPath();
		if(!m_AlignedPool)
		{
			Start();
		}
		size_t obHops = m_AlignedPool->GetNumOutboundHops();
		auto path = std::make_shared<i2p::garlic::GarlicRoutingPath>();
		auto tuns = m_AlignedPool->GetOutboundTunnelsWhere([obHops](std::shared_ptr<i2p::tunnel::OutboundTunnel> tun) -> bool {
				return tun && tun->GetPeers().size() > obHops;
		}, false);
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
		if(!path->outboundTunnel)
		{
			for(auto & tun : tuns)
			{
				if(tun->IsEstablished())
				{
					path->outboundTunnel = tun;
					break;
				}
			}
		}
		path->remoteLease = m_CurrentRemoteLease;
		SetSharedRoutingPath(path);
		return path;
	}

	i2p::data::IdentHash AlignedDestination::GetIBGWFor(const RemoteDestination_t & ident, Lease_ptr fallback)
	{
		i2p::data::IdentHash gateway;
		{
			std::unique_lock<std::mutex> lock(m_DestinationLeasesMutex);
			auto itr = m_DestinationLeases.find(ident);
			if(itr == m_DestinationLeases.end())
			{
				m_DestinationLeases[ident] = fallback->tunnelGateway;
			}
			gateway = m_DestinationLeases[ident];
		}
		return gateway;
	}

	void AlignedRoutingSession::Start()
	{
		if(!m_AlignedPool)
		{
			auto pool = GetOwner()->GetTunnelPool();
			m_AlignedPool = i2p::tunnel::tunnels.CreateTunnelPool(pool->GetNumInboundHops(), pool->GetNumOutboundHops(), 0, 2);
			m_AlignedPool->SetCustomPeerSelector(this);
			m_AlignedPool->UseBidirectionalTunnels(true);
			m_AlignedPool->SetLocalDestination(pool->GetLocalDestination());
			LogPrint(eLogDebug, "AlignedRoutingSession: created tunnel pool");
		}
	}

}
}
