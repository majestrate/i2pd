#ifndef ALIGNED_H
#define ALIGNED_H
#include "Destination.h"

namespace i2p
{
namespace client
{

  class AlignedDestination;

  class AlignedRoutingSession : public i2p::garlic::GarlicRoutingSession , public i2p::tunnel::ITunnelPeerSelector
  {
  public:
    typedef std::function<void(void)> BuildCompleteCallback;
    typedef std::shared_ptr<const i2p::data::Lease> Lease_ptr;

    AlignedRoutingSession (AlignedDestination * owner, std::shared_ptr<const i2p::data::RoutingDestination> destination,
                           int numTags, bool attachLeaseSet);
    ~AlignedRoutingSession();
    std::shared_ptr<i2p::garlic::GarlicRoutingPath> GetSharedRoutingPath();
    bool SelectPeers(i2p::tunnel::Path & peers, int hops, bool inbound);
    bool OnBuildResult(const i2p::tunnel::Path & peers, bool isInbound, i2p::tunnel::TunnelBuildResult result);
    void SetCurrentLease(const Lease_ptr & lease) { m_CurrentRemoteLease = lease; };
    void SetBuildCompleteCallback(BuildCompleteCallback buildComplete) { m_BuildCompleted = buildComplete; };
  private:
    BuildCompleteCallback m_BuildCompleted;
    std::shared_ptr<i2p::tunnel::TunnelPool> m_AlignedPool;
    Lease_ptr m_CurrentRemoteLease;
  };

  typedef std::shared_ptr<AlignedRoutingSession> AlignedRoutingSession_ptr;

  class AlignedDestination : public ClientDestination
  {
  public:
    AlignedDestination(const i2p::data::PrivateKeys& keys, bool isPublic, const std::map<std::string, std::string> * params);
    ~AlignedDestination();
    void CreateStream(StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash& dest, int port = 0);
    std::shared_ptr<i2p::stream::Stream> CreateStream (std::shared_ptr<const i2p::data::LeaseSet> remote, int port = 0);
    void PrepareOutboundTunnelTo(const RemoteDestination_t & gateway, RoutingDestination_ptr remote);
    OBTunnel_ptr GetAlignedTunnelTo(const RemoteDestination_t & gateway);
    OBTunnel_ptr GetNewOutboundTunnel(OBTunnel_ptr exlcuding=nullptr) { return GetTunnelPool()->GetNewOutboundTunnel(exlcuding); }
    OBTunnel_ptr GetOutboundTunnelFor(const RemoteDestination_t & destination, OBTunnel_ptr excluding=nullptr);

  protected:

    i2p::garlic::GarlicRoutingSessionPtr CreateNewRoutingSession(std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLeaseSet);

    typedef std::function<void(AlignedRoutingSession_ptr)> AlignedPathObtainedFunc;

  private:

    typedef std::shared_ptr<const i2p::data::Lease> Lease_ptr;
    void ObtainAlignedRoutingPath(RoutingDestination_ptr destination, const RemoteDestination_t & gateway,  bool attachLS, AlignedPathObtainedFunc obtained);
    void HandleGotAlignedRoutingPathForStream(AlignedRoutingSession_ptr session, StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port);


    bool HasOutboundTunnelTo(const i2p::data::IdentHash & gateway);
  };

}
}

#endif
