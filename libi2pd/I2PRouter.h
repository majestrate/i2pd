#ifndef I2P_ROUTER_H
#define I2P_ROUTER_H
#include "Identity.h"
#include "I2NPProtocol.h"

namespace i2p
{

  // foward declaired
  struct I2NPMessage;

  struct RouterImpl;

  class Router : public i2p::data::RoutingDestination
  {

  public:
    Router();
    ~Router();

    bool Init();
    void Start();
    void Stop();

    void HandleI2NPMessage(uint8_t * data, size_t sz);
    void HandleI2NPMessage(std::shared_ptr<I2NPMessage> msg);


    // implements RoutingDestination
    const i2p::data::IdentHash & GetIdentHash() const;
    const uint8_t * GetEncryptionPublicKey() const;
    bool IsDestination() const { return false; };

    void SendTo(const i2p::data::IdentHash & ident, std::shared_ptr<I2NPMessage> msg);
    bool HasRouterByIdentHash(const i2p::data::IdentHash & ident);

    void PostTunnelData(std::shared_ptr<I2NPMessage> msg);
    void PostTunnelData(std::vector<std::shared_ptr<I2NPMessage> > & msgs);

    void PostNetdbMessage(std::shared_ptr<I2NPMessage> msg);

    void SetMaxNumTransitTunnels(uint16_t num) { m_MaxTransitTunnels = num; }

    void ProcessDeliveryStatusMessage(std::shared_ptr<I2NPMessage> msg);

    void ProcessGarlicMessageForContext(std::shared_ptr<I2NPMessage> msg);

    std::shared_ptr<I2NPMessage> CreateDeliveryStatusMsg (uint32_t msgID);
    std::shared_ptr<I2NPMessage> CreateDatabaseSearchReply (const i2p::data::IdentHash& ident, std::vector<i2p::data::IdentHash> routers);
    std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsgFromUs (std::shared_ptr<const i2p::data::RouterInfo> router = nullptr, uint32_t replyToken = 0);

  private:
    bool HandleBuildRequestRecords (int num, uint8_t * records, uint8_t * clearText);
    void HandleVariableTunnelBuildMsg (uint32_t replyMsgID, uint8_t * buf, size_t len);
    void HandleVariableTunnelBuildReplyMsg (uint32_t replyMsgID, uint8_t * buf, size_t len);
    void HandleTunnelBuildMsg (uint8_t * buf, size_t len);
    RouterImpl * impl;
    uint16_t m_MaxTransitTunnels;
  };
}
#endif
