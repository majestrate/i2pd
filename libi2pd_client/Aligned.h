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
    void SetCurrentLease(const Lease_ptr & lease);
    void AddBuildCompleteCallback(BuildCompleteCallback buildComplete);
    void Start();
  private:
    std::mutex m_BuildCompletedMutex;
    std::vector<BuildCompleteCallback> m_BuildCompleted;
    std::shared_ptr<i2p::tunnel::TunnelPool> m_AlignedPool;
    Lease_ptr m_CurrentRemoteLease;
    AlignedDestination * m_Parent;
  };

  typedef std::shared_ptr<AlignedRoutingSession> AlignedRoutingSession_ptr;

  class AlignedDestination : public ClientDestination
  {
  public:
    AlignedDestination(const i2p::data::PrivateKeys& keys, bool isPublic, const std::map<std::string, std::string> * params);
    ~AlignedDestination();
    virtual void CreateStream(StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash& dest, int port = 0) override;
    virtual void PrepareOutboundTunnelTo(const RemoteDestination_t & gateway, RoutingDestination_ptr remote) override;
    virtual OBTunnel_ptr GetAlignedTunnelTo(const RemoteDestination_t & gateway, OBTunnel_ptr excluding) override;
    virtual OBTunnel_ptr GetNewOutboundTunnel(OBTunnel_ptr exlcuding) override;
    virtual OBTunnel_ptr GetOutboundTunnelFor(const RemoteDestination_t & destination, OBTunnel_ptr excluding=nullptr) override;

  protected:

    virtual i2p::garlic::GarlicRoutingSessionPtr CreateNewRoutingSession(std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLeaseSet) override;

    typedef std::function<void(AlignedRoutingSession_ptr)> AlignedPathObtainedFunc;

  private:

    typedef std::shared_ptr<const i2p::data::Lease> Lease_ptr;
    void ObtainAlignedRoutingPath(RoutingDestination_ptr destination, const RemoteDestination_t & gateway,  bool attachLS, AlignedPathObtainedFunc obtained);
    void HandleGotAlignedRoutingPathForStream(AlignedRoutingSession_ptr session, StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port);

    // maps destination -> IBGW
    std::map<i2p::data::IdentHash, i2p::data::IdentHash> m_DestinationLeases;
    std::mutex m_DestinationLeasesMutex;

    i2p::data::IdentHash GetIBGWFor(const RemoteDestination_t & ident, Lease_ptr fallback);

    bool HasOutboundTunnelTo(const i2p::data::IdentHash & gateway);
  };

}
}

#endif
