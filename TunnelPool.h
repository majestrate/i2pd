#ifndef TUNNEL_POOL__
#define TUNNEL_POOL__

#include <inttypes.h>
#include <set>
#include <vector>
#include <utility>
#include <mutex>
#include <memory>
#include "Identity.h"
#include "LeaseSet.h"
#include "RouterInfo.h"
#include "I2NPProtocol.h"
#include "TunnelBase.h"
#include "RouterContext.h"
#include "Garlic.h"
#include "TunnelConfig.h"

namespace i2p
{
namespace tunnel
{
	class Tunnel;
	class InboundTunnel;
	class OutboundTunnel;

	enum TunnelBuildResult
	{
		eBuildResultOkay,
		eBuildResultTimeout,
		eBuildResultRejected
	};
	 
  
	/** interface for custom tunnel peer selection algorithm */
	struct ITunnelPeerSelector
	{
		typedef std::shared_ptr<const i2p::data::IdentityEx> Peer;
		typedef std::vector<Peer> TunnelPath;
		virtual bool SelectPeers(TunnelPath & peers, int hops, bool isInbound) = 0;
		virtual bool OnBuildResult(TunnelPath & peer, bool isInbound, TunnelBuildResult result) = 0;
	};

	typedef std::shared_ptr<ITunnelPeerSelector> TunnelPeerSelector;
  
	class TunnelPool: public std::enable_shared_from_this<TunnelPool> // per local destination
	{
		public:

			TunnelPool (int numInboundHops, int numOutboundHops, int numInboundTunnels, int numOutboundTunnels);
			~TunnelPool ();
		
			std::shared_ptr<i2p::garlic::GarlicDestination> GetLocalDestination () const { return m_LocalDestination; };
			void SetLocalDestination (std::shared_ptr<i2p::garlic::GarlicDestination> destination) { m_LocalDestination = destination; };
			void SetExplicitPeers (std::shared_ptr<std::vector<i2p::data::IdentHash> > explicitPeers);

      void RequireLatency(const uint64_t min, const uint64_t max);
      
			void CreateTunnels ();
			void TunnelCreated (std::shared_ptr<InboundTunnel> createdTunnel);
			void TunnelExpired (std::shared_ptr<InboundTunnel> expiredTunnel);
			void TunnelCreated (std::shared_ptr<OutboundTunnel> createdTunnel);
			void TunnelExpired (std::shared_ptr<OutboundTunnel> expiredTunnel);
			void RecreateInboundTunnel (std::shared_ptr<InboundTunnel> tunnel);
			void RecreateOutboundTunnel (std::shared_ptr<OutboundTunnel> tunnel);
			std::vector<std::shared_ptr<InboundTunnel> > GetInboundTunnels (int num) const;
    std::shared_ptr<OutboundTunnel> GetNextOutboundTunnel (std::shared_ptr<OutboundTunnel> excluded = nullptr, bool ignoreLatency=true) const;
    std::shared_ptr<InboundTunnel> GetNextInboundTunnel (std::shared_ptr<InboundTunnel> excluded = nullptr, bool ignoreLatency=true) const;		
    std::shared_ptr<OutboundTunnel> GetNewOutboundTunnel (std::shared_ptr<OutboundTunnel> old, bool ignoreLatency=true) const;
    
			void TestTunnels ();
			void ProcessGarlicMessage (std::shared_ptr<I2NPMessage> msg);
			void ProcessDeliveryStatus (std::shared_ptr<I2NPMessage> msg);

			bool IsActive () const { return m_IsActive; };
			void SetActive (bool isActive) { m_IsActive = isActive; };
			void DetachTunnels ();

			int GetNumInboundTunnels () const { return m_NumInboundTunnels; };
			int GetNumOutboundTunnels () const { return m_NumOutboundTunnels; };

			void SetCustomPeerSelector(TunnelPeerSelector selector);
			void UnsetCustomPeerSelector();
			bool HasCustomPeerSelector();
			template<class TTunnel>
			void OnTunnelBuildResult(TTunnel& tunnel, TunnelBuildResult result);
		private:
			void CreateInboundTunnel ();	
			void CreateOutboundTunnel ();
			void CreatePairedInboundTunnel (std::shared_ptr<OutboundTunnel> outboundTunnel);
			template<class TTunnels>
			typename TTunnels::value_type GetNextTunnel (TTunnels& tunnels, typename TTunnels::value_type excluded, bool ignoreLatency=true) const;
			std::shared_ptr<const i2p::data::RouterInfo> SelectNextHop (std::shared_ptr<const i2p::data::RouterInfo> prevHop) const;
			bool SelectPeers (std::vector<std::shared_ptr<const i2p::data::IdentityEx> >& hops, bool isInbound);
			bool SelectExplicitPeers (std::vector<std::shared_ptr<const i2p::data::IdentityEx> >& hops, bool isInbound);			

		private:

			std::shared_ptr<i2p::garlic::GarlicDestination> m_LocalDestination;
			int m_NumInboundHops, m_NumOutboundHops, m_NumInboundTunnels, m_NumOutboundTunnels;
			std::shared_ptr<std::vector<i2p::data::IdentHash> > m_ExplicitPeers;	
			mutable std::mutex m_InboundTunnelsMutex;
			std::set<std::shared_ptr<InboundTunnel>, TunnelCreationTimeCmp> m_InboundTunnels; // recent tunnel appears first
			mutable std::mutex m_OutboundTunnelsMutex;
			std::set<std::shared_ptr<OutboundTunnel>, TunnelCreationTimeCmp> m_OutboundTunnels;
			mutable std::mutex m_TestsMutex;
			std::map<uint32_t, std::pair<std::shared_ptr<OutboundTunnel>, std::shared_ptr<InboundTunnel> > > m_Tests;
			bool m_IsActive;
			std::mutex m_CustomPeerSelectorMutex;
			TunnelPeerSelector m_CustomPeerSelector;
      uint64_t m_MinLatency;
      uint64_t m_MaxLatency;
    
		public:

			// for HTTP only
			const decltype(m_OutboundTunnels)& GetOutboundTunnels () const { return m_OutboundTunnels; };
			const decltype(m_InboundTunnels)& GetInboundTunnels () const { return m_InboundTunnels; };

	};	
}
}

#endif

