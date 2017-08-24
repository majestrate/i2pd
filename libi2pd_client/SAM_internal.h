#ifndef SAM_INTERNAL_H
#define SAM_INTERNAL_H
#include "SAM.h"
#include <array>
#include <map>


namespace std
{
  /** TODO: remove this when we go to c++14 */
  template<typename T, typename... Args>
  unique_ptr<T> make_unique(Args&&... args)
  {
    return unique_ptr<T>(new T(forward<Args>(args)...));
  }
}

namespace i2p
{
  namespace client
  {
    namespace sam
    {

      typedef boost::asio::ip::tcp::acceptor tcp_acceptor_t;
      typedef std::unique_ptr<tcp_acceptor_t> tcp_acceptor_ptr;

      constexpr std::size_t socket_buffer_size = 65536;

      struct Session
      {
        typedef std::array<uint8_t, socket_buffer_size> socket_buffer_t;
        SessionInfo_ptr GetInfo() const;
        std::string ID;
        tcp_socket_t socket;

        socket_buffer_t m_StreamBuff;
        socket_buffer_t m_SocketBuff;
      };

      typedef std::unique_ptr<Session> Session_ptr;

      struct BridgeImpl
      {
        BridgeImpl(const std::string & addr, uint16_t port);
        ~BridgeImpl();


        void Start();
        void Stop();
        void Run();

        SessionInfo_ptr FindSession(const std::string & id) const;
        std::list<SessionInfo_ptr> ListSessions() const;

        typedef std::function<void(const Session_ptr &)> SessionVisitor;

        typedef std::mutex mtx_t;
        typedef std::unique_lock<mtx_t> lock_t;

        void ForEachSession(SessionVisitor v)
        {
          lock_t l(m_SessionsMutex);
          for(const auto & itr : m_Sessions)
            v(itr.second);
        }

        mtx_t m_SessionsMutex;
        std::map<std::string, Session_ptr> m_Sessions;

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
