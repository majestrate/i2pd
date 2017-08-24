#ifndef SAM_INTERNAL_H
#define SAM_INTERNAL_H
#include "SAM.h"
namespace i2p
{
  namespace client
  {
    namespace sam
    {

      typedef boost::asio::ip::tcp::acceptor tcp_acceptor_t;
      typedef std::shared_ptr<tcp_acceptor_t> tcp_acceptor_ptr;

      struct BridgeImpl
      {
        BridgeImpl(const std::string & addr, uint16_t port);
        ~BridgeImpl();


        void Start();
        void Stop();

        void Run();

        SessionInfo_ptr FindSession(const std::string & id) const;
        std::list<SessionInfo_ptr> ListSessions() const;

        std::thread * m_Thread = nullptr;
        tcp_endpoint_t m_Endpoint;
        tcp_acceptor_ptr m_Acceptor = nullptr;
        bool m_Running = false;
        boost::asio::io_service m_Service;
      };
    }
  }
}
#endif
