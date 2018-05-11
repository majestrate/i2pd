#include "NTCP2Session.h"

namespace i2p
{
  namespace transport
  {
    const int NTCP2_SESSION_TIMEOUT = 30;

    NTCP2Session::NTCP2Session(NTCP2Server & server, RI_cptr router) : 
      TransportSession(router, NTCP2_SESSION_TIMEOUT),
      m_Server(server), 
      m_Socket(server.GetService()),
      m_IsEstablished(false), m_IsTerminated(false)
    {

    }

    NTCP2Session::~NTCP2Session()
    {

    }

    void NTCP2Session::Terminate()
    {

    }

    void NTCP2Session::ClientLogin()
    {

    }

    void NTCP2Session::ServerLogin()
    {

    }

    void NTCP2Session::Done()
    {

    }

    NTCP2Session::Service_t & NTCP2Session::GetService()
    {
      return m_Server.GetService();
    }

    void NTCP2Session::SendI2NPMessages(const I2NPMessageList & msgs)
    {
      m_Server.GetService().post(std::bind(&NTCP2Session::PostMessages, shared_from_this(), msgs));
    }

    void NTCP2Session::PostMessages(I2NPMessageList msgs)
    {

    }

  }
}