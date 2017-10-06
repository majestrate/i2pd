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
    typedef i2p::data::IdentHash Gateway_t;

    AlignedRoutingSession (AlignedDestination * owner, std::shared_ptr<const i2p::data::RoutingDestination> destination,
                           int numTags, bool attachLeaseSet);
    ~AlignedRoutingSession();
    std::shared_ptr<i2p::garlic::GarlicRoutingPath> GetSharedRoutingPath();
    bool SelectPeers(i2p::tunnel::Path & peers, int hops, bool inbound);
    bool OnBuildResult(const i2p::tunnel::Path & peers, bool isInbound, i2p::tunnel::TunnelBuildResult result);
    void AddBuildCompleteCallback(BuildCompleteCallback buildComplete);
    void Start();
    std::shared_ptr<i2p::tunnel::TunnelPool> GetTunnelPool () const { return m_AlignedPool; };
    void SetIBGW(const Gateway_t & ibgw) { m_IBGW = ibgw; };
  private:
    void UpdateIBGW();
    std::mutex m_BuildCompletedMutex;
    std::vector<BuildCompleteCallback> m_BuildCompleted;
    std::shared_ptr<i2p::tunnel::TunnelPool> m_AlignedPool;
    Gateway_t m_IBGW;
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
    void ObtainAlignedRoutingPath(RoutingDestination_ptr destination, const RemoteDestination_t & gateway,  bool attachLS, AlignedPathObtainedFunc obtained);
    void HandleGotAlignedRoutingPathForStream(AlignedRoutingSession_ptr session, StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, int port);

    bool HasOutboundTunnelTo(const i2p::data::IdentHash & gateway);
  };

}
}

#endif
