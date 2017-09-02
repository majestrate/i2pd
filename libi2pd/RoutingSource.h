#ifndef ROUTING_SOURCE_H
#define ROUTING_SOURCE_H
#include "Identity.h"
#include "RouterInfo.h"
namespace i2p
{
namespace data
{
  class RoutingSource
  {
  public:
    virtual ~RoutingSource();
    virtual std::shared_ptr<RouterInfo> FindRouter(const IdentHash & ident) const = 0;
    virtual std::shared_ptr<RouterInfo> FindLeaseSet(const IdentHash & ident) const = 0;
    virtual std::shared_ptr<IdentityEx> GetIdentity() = 0;
    const IdentHash & GetIdentHash() { return GetIdentity()->GetIdentHash(); };
    virtual bool RequestDestination(const IdentHash & destination, RemoteRouterInfoVisitor requestComplete = nullptr) = 0;
  };
}
}
#endif
