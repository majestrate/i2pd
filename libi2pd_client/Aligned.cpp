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

	/** high level api */
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
				auto gateway = leases[rand() % leases.size()]->tunnelGateway;
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
		auto session = std::static_pointer_cast<AlignedRoutingSession>(GetRoutingSession(remote, true));
		session->Start();
		auto pool = GetTunnelPool();
		auto obtun = pool->GetNextOutboundTunnel();
		auto ri = i2p::data::netdb.FindRouter(gateway);
		if(obtun)
		{
			auto foundIBGW = [obtun, session](std::shared_ptr<const i2p::data::RouterInfo> ibgw)
			{
				
				if(ibgw && !session->GetTunnelPool()->GetOutboundTunnelsWhere([ibgw](OBTunnel_ptr tun) -> bool { return tun->GetEndpointIdentHash() == ibgw->GetIdentHash(); } ).size())
				{
					LogPrint(eLogDebug, "Aligned: found IBGW building immediate ob tunnel");
					auto peers = obtun->GetPeers();
					peers.push_back(ibgw->GetRouterIdentity());
					session->GetTunnelPool()->CreateOutboundTunnelImmediate(peers);
				}
			};
			if(ri)
				foundIBGW(ri);
			else
				i2p::data::netdb.RequestDestination(gateway, foundIBGW);
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
		session->Start();
		session->SetIBGW(gateway);
		if(!session->GetTunnelPool()->GetOutboundTunnels().size())
			PrepareOutboundTunnelTo(gateway, destination);
				
		auto gotLease = [=](std::shared_ptr<const i2p::data::LeaseSet> ls) {
			if(!ls) {
				LogPrint(eLogWarning, "AlignedDestination: cannot resolve lease set");
				return;
			}
			auto gotRouter = [=](std::shared_ptr<i2p::data::RouterInfo> ri) {
				if(!ri) {
					LogPrint(eLogWarning, "AlignedDestinatino failed to find IBGW");
					obtained(nullptr);
					return;
				}
				session->AddBuildCompleteCallback([=](){
					obtained(session);
				});
			};
			auto obep = i2p::data::netdb.FindRouter(gateway);
			if(obep)
				gotRouter(obep);
			else
				i2p::data::netdb.RequestDestination(gateway, gotRouter);
		};
		gotLease(FindLeaseSet(destination->GetIdentHash()));
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
		UpdateIBGW();
		std::vector<std::shared_ptr<i2p::tunnel::OutboundTunnel> > tuns;
		if(m_AlignedPool)
		{
			tuns = m_AlignedPool->GetOutboundTunnelsWhere([&](std::shared_ptr<i2p::tunnel::OutboundTunnel> tun) -> bool {
					return tun && tun->IsEstablished() && tun->GetEndpointIdentHash() == m_IBGW;
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
			if(obep == m_IBGW)
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

	void AlignedRoutingSession::UpdateIBGW()
	{
		VisitSharedRoutingPath([&](std::shared_ptr<i2p::garlic::GarlicRoutingPath> p) { if(p && p->remoteLease) m_IBGW = p->remoteLease->tunnelGateway; } );
	}
	
	bool AlignedRoutingSession::SelectPeers(i2p::tunnel::Path & path, int hops, bool inbound)
	{
		UpdateIBGW();
		auto selectNextHop = std::bind(&i2p::tunnel::TunnelPool::SelectNextHop, m_AlignedPool, std::placeholders::_1);
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
