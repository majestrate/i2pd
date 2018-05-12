#ifndef LIBI2PD_NTCP2_H
#define LIBI2PD_NTCP2_H

#include <boost/asio.hpp>
#include <memory>
#include <list>
#include <thread>

#include "Identity.h"

namespace i2p
{
namespace crypto
{
	class NTCP2PrivateKeys
	{
  public:
    const static size_t BufferSize = 64;
    NTCP2PrivateKeys () = default;
    ~NTCP2PrivateKeys () = default;
    NTCP2_Key ToPublicKey();
    NTCP2_IV & IV() { return m_IV; };
    size_t FromBuffer(const uint8_t * buf, size_t sz);
    size_t ToBuffer(uint8_t *buf, size_t sz) const;

    /** randomize IV */
    void RotateIV() { m_IV.Randomize(); };

    /** generate new seed and IV */
    void Generate();

  private:

		NTCP2_Key m_Seed;
    NTCP2_IV m_IV;
    std::unique_ptr<NTCP2_Key> m_PubKey;
	};
}

namespace data
{
  const char NTCP2_PROTOCOL_NAME[] = "NXK2CS";
}

namespace transport
{

  class NTCP2Session;

  class NTCP2Server
  {
    public:

      typedef boost::asio::io_service Service_t;
      typedef boost::asio::ip::address IP_t;
      typedef boost::asio::ip::tcp::acceptor Acceptor_t;
      typedef boost::asio::ip::tcp::endpoint Endpoint_t;
      typedef boost::asio::ip::tcp::socket Socket_t;
      typedef boost::asio::deadline_timer Timer_t;
      typedef std::shared_ptr<Timer_t> Timer_ptr;
      typedef boost::system::error_code error_t;

      typedef std::shared_ptr<NTCP2Session> Session_ptr;
      typedef i2p::data::IdentHash Ident;

      NTCP2Server();
      NTCP2Server(NTCP2Server && ) = delete;
      ~NTCP2Server();

      void Start();
      void Stop();

      Service_t & GetService() { return m_Service; };

      bool AddSession(Session_ptr conn);
      void RemoveSession(Session_ptr conn);
      Session_ptr FindSession(const Ident & remote);

      template<typename Address>
      void Connect(std::shared_ptr<const Address> addr, Session_ptr conn);

    private:
      typedef std::map<Ident, Session_ptr> SessionMap_t;

      bool m_IsRunning;
      std::thread * m_Thread;
      Service_t m_Service;
      Service_t::work m_Work;
      Timer_t m_TerminationTimer;
      Acceptor_t * m_V4Acceptor, * m_V6Acceptor;
      SessionMap_t m_Sessions;

      std::list<Session_ptr> m_PendingIncomingSessions;

    private:

      void Run();

      void HandleAccept(Acceptor_t * a, Session_ptr conn, const error_t & ec);
      void HandleConnect(const error_t & ec, Session_ptr conn, Timer_ptr timer);

      void ScheduleTermination();
      void HandleTerminationTimer(const error_t & ec);
  };
}  
}

#endif