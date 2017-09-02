#include "I2NPMessageHandler.h"

namespace i2p
{

	I2NPMessagesHandler::~I2NPMessagesHandler ()
	{
		Flush ();
	}

	void I2NPMessagesHandler::PutNextMessage (std::shared_ptr<I2NPMessage>  msg)
	{
		if (msg)
		{
			switch (msg->GetTypeID ())
			{
			case eI2NPTunnelData:
				m_TunnelMsgs.push_back (msg);
				break;
			case eI2NPTunnelGateway:
				m_TunnelGatewayMsgs.push_back (msg);
				break;
			default:
				m_Handler->HandleI2NPMessage (msg);
			}
		}
	}

	void I2NPMessagesHandler::Flush ()
	{
		if (!m_TunnelMsgs.empty ())
		{
			m_Handler->PostTunnelData (m_TunnelMsgs);
			m_TunnelMsgs.clear ();
		}
		if (!m_TunnelGatewayMsgs.empty ())
		{
			m_Router->PostTunnelData (m_TunnelGatewayMsgs);
			m_TunnelGatewayMsgs.clear ();
		}
	}
}
