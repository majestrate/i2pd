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
		{
			LogPrint(eLogDebug, "AlignedDestination: got aligned path");
			ClientDestination::CreateStream(streamRequestComplete, dest, port);
		}
		else
			streamRequestComplete(nullptr);
	}

	/** high level api */
	void AlignedDestination::CreateStream(StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port)
	{
		LogPrint(eLogDebug, "AlignedDestination: create stream to ",dest.ToBase32(), ":", port);
		auto self = std::static_pointer_cast<AlignedDestination>(shared_from_this());
		auto gotLeaseSet = [self, streamRequestComplete, dest, port](std::shared_ptr<const i2p::data::LeaseSet> ls ) {
			if(!ls)
			{
				LogPrint(eLogDebug, "AlignedDestination: No LeaseSet");
				streamRequestComplete(nullptr);
				return;
			}
			auto leases = ls->GetNonExpiredLeases ();
			if(leases.size())
			{
				auto gateway = leases[rand() % leases.size()]->tunnelGateway;
				self->ObtainAlignedRoutingPath(ls, gateway, true, [self, streamRequestComplete, dest, port](AlignedRoutingSession_ptr s) {
						self->HandleGotAlignedRoutingPathForStream(s, streamRequestComplete, dest, port);
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
		auto session = std::static_pointer_cast<AlignedRoutingSession>(GetRoutingSession(remote, true));
		session->Start();
		session->SetIBGW(gateway);
		auto pool = GetTunnelPool();
		auto obtun = pool->GetNextOutboundTunnel();
		if(obtun && !session->HasTunnelsReady())
		{
			auto foundIBGW = [obtun, session](std::shared_ptr<const i2p::data::RouterInfo> ibgw)
			{
				if(ibgw)
				{
					auto peers = obtun->GetPeers();
					peers.push_back(ibgw->GetRouterIdentity());
					session->CreateOutboundTunnelImmediate(peers);
				}
			};
			i2p::data::netdb.RequestDestination(gateway, foundIBGW);
			session->MarkAsBuilding();
		}
	}

	bool AlignedRoutingSession::HasTunnelsReady()
	{
		return GetTunnelPool()->GetOutboundTunnelsWhere([&](std::shared_ptr<i2p::tunnel::OutboundTunnel> tun) -> bool { return tun->GetEndpointIdentHash() == m_IBGW; } ).size() > 0 || m_Building;
	}

	void AlignedRoutingSession::CreateOutboundTunnelImmediate(const std::vector<std::shared_ptr<const i2p::data::IdentityEx> > & peers)
	{
		if(!m_Building)
		{
			MarkAsBuilding();
			LogPrint(eLogDebug, "Aligned: found IBGW building immediate ob tunnel");
			GetTunnelPool()->CreateOutboundTunnelImmediate(peers);
		}
	}

	i2p::garlic::GarlicRoutingSessionPtr AlignedDestination::CreateNewRoutingSession(std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLS)
	{
		return std::make_shared<AlignedRoutingSession>(this, destination, numTags, attachLS);
	}

	void AlignedDestination::ObtainAlignedRoutingPath(RoutingDestination_ptr destination, const RemoteDestination_t & gateway, bool attachLS, AlignedPathObtainedFunc obtained)
	{
		LogPrint(eLogDebug, "AlignedDestination: obtain routing path to IBGW=", gateway.ToBase64());
		auto session = std::static_pointer_cast<AlignedRoutingSession>(GetRoutingSession(destination, attachLS));
		PrepareOutboundTunnelTo(gateway, destination);
		auto gotLeaseSet = [obtained, session](std::shared_ptr<const i2p::data::LeaseSet> ls) {
			if(!ls) {
				LogPrint(eLogWarning, "AlignedDestination: cannot resolve lease set");
				obtained(nullptr);
				return;
			}
			session->AddBuildCompleteCallback(std::bind(obtained, session));
		};
		RequestDestination(destination->GetIdentHash(), gotLeaseSet);
	}


	AlignedRoutingSession::AlignedRoutingSession(AlignedDestination * owner, std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLeaseSet) :
		i2p::garlic::GarlicRoutingSession(owner, destination, numTags, attachLeaseSet),
		m_Parent(owner),
		m_AlignedPool(nullptr),
		m_Building(false)
	{
		m_IBGW.Fill(0);
	}

	AlignedRoutingSession::~AlignedRoutingSession()
	{
		if(m_AlignedPool)
		{
			m_AlignedPool->SetCustomPeerSelector(nullptr);
			i2p::tunnel::tunnels.DeleteTunnelPool(m_AlignedPool);
		}
	}

	void AlignedRoutingSession::SetIBGW(const Gateway_t & ibgw)
	{
		if((!m_IBGW.IsZero()) && ibgw != m_IBGW)
		{
			m_Building = false;
		}
		m_IBGW = ibgw;
	}
	
	void AlignedRoutingSession::AddBuildCompleteCallback(BuildCompleteCallback buildComplete)
	{
		UpdateIBGW();
		std::vector<std::shared_ptr<i2p::tunnel::OutboundTunnel> > tuns;
		if(m_AlignedPool)
		{
			tuns = m_AlignedPool->GetOutboundTunnelsWhere([&](std::shared_ptr<i2p::tunnel::OutboundTunnel> tun) -> bool {
					return tun && tun->IsEstablished() && tun->GetEndpointIdentHash() == m_IBGW;
				}, false);
		}
		if(!tuns.size() || m_Building)
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
			std::vector<BuildCompleteCallback> calls;
			{
				std::unique_lock<std::mutex> lock(m_BuildCompletedMutex);
				m_Building = false;
				for(auto & cb : m_BuildCompleted)
					calls.push_back(cb);
				m_BuildCompleted.clear();
			}
			for(auto & cb : calls) cb();
		}
		return true;
	}

	void AlignedRoutingSession::UpdateIBGW()
	{
		VisitSharedRoutingPath([&](std::shared_ptr<i2p::garlic::GarlicRoutingPath> p) {
			if(!p) return;
			if(p->remoteLease)
			{
				if(m_IBGW != p->remoteLease->tunnelGateway)
				{
					// IBGW changed
					m_Building = false;
					m_IBGW = p->remoteLease->tunnelGateway;
				}
				else if (p->outboundTunnel && p->outboundTunnel->ExpiresSoon())
				{
					// outbound tunnel is expiring or dead
					m_Building = false;
				}
			}
		});
	}

	void AlignedRoutingSession::MarkAsBuilding()
	{
		m_Building = true;
	}
	
	bool AlignedRoutingSession::SelectPeers(i2p::tunnel::Path & path, int hops, bool inbound)
	{
		UpdateIBGW();
		auto selectNextHop = [](std::shared_ptr<const i2p::data::RouterInfo> prevHop) -> std::shared_ptr<const i2p::data::RouterInfo> {
			return i2p::data::netdb.GetHighBandwidthRandomRouter(prevHop);
		};
		if(!i2p::tunnel::StandardSelectPeers(path, hops, inbound, &i2p::tunnel::StandardSelectFirstHop, selectNextHop))
			return false;
		std::shared_ptr<i2p::data::RouterInfo> obep = nullptr;
		if(!inbound)
		{
			obep = i2p::data::netdb.FindRouter(m_IBGW);
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
		else if(m_AlignedPool == nullptr)
			return std::make_shared<i2p::garlic::GarlicRoutingPath>();
		
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
		SetSharedRoutingPath(path);
		return path;
	}

	void AlignedRoutingSession::Start()
	{
		if(!m_AlignedPool)
		{
			const auto pool = GetOwner()->GetTunnelPool();
			m_AlignedPool = i2p::tunnel::tunnels.CreateTunnelPool(pool->GetNumInboundHops(), pool->GetNumOutboundHops(), 1, 2);
			m_AlignedPool->SetCustomPeerSelector(this);
			m_AlignedPool->UseBidirectionalTunnels(true);
			m_AlignedPool->SetLocalDestination(m_Parent->GetSharedFromThis());
			pool->CopySettingsInto(m_AlignedPool);
			LogPrint(eLogDebug, "AlignedRoutingSession: created tunnel pool");
		}
	}

}
}
