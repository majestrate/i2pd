#ifndef I2NP_MESSAGE_HANDLER_H
#define I2NP_MESSAGE_HANDLER_H
#include "I2PRouter.h"
namespace i2p
{

  /** buffers tunneldata and tunnelgateway messages */
  class I2NPMessagesHandler
  {
  public:

    I2NPMessagesHandler(I2NPHandler * h) : m_Handler(h) {}
    ~I2NPMessagesHandler ();
    void PutNextMessage (std::shared_ptr<I2NPMessage> msg);
    void Flush ();

  private:

    i2p::I2NPHandler* m_Handler;
    std::vector<std::shared_ptr<I2NPMessage> > m_TunnelMsgs, m_TunnelGatewayMsgs;
  };

}
#endif
