#ifndef I2NP_HPP
#define I2NP_HPP

#include <memory>

namespace i2p
{
	// forward declaired
	struct I2NPMessage;

	/** handler of inbound i2np messages */
  struct I2NPHandler
  {
    typedef std::shared_ptr<I2NPMessage> Msg_ptr;
		typedef std::vector<Msg_ptr> ManyMsg_t;
		virtual void HandleI2NPMessage(Msg_ptr msg) = 0;
		virtual void PostTunnelData(const ManyMsg_t & msgs) = 0;
  };

	struct I2NPSender
	{
		typedef std::shared_ptr<I2NPMessage> Msg_ptr;
		typedef std::shared_ptr<const i2p::data::RouterInfo> RI_ptr;
		/** queue outbound send to operation */
		virtual void PutI2NPMessage(RI_ptr routerIdent, Msg_ptr msg) = 0;
	};
}


#endif
