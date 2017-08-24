#ifndef SAM_H__
#define SAM_H__

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <list>

#include "Destination.h"

namespace i2p
{
  namespace client
  {

    namespace sam
    {
      typedef boost::asio::ip::tcp::socket tcp_socket_t;
      typedef std::shared_ptr<tcp_socket_t> tcp_socket_ptr;
      typedef boost::asio::ip::tcp::endpoint tcp_endpoint_t;
      typedef std::shared_ptr<tcp_endpoint_t> tcp_endpoint_ptr;

      typedef boost::asio::ip::udp::socket udp_socket_t;
      typedef boost::asio::ip::udp::endpoint udp_endpoint_t;
      typedef std::shared_ptr<udp_endpoint_t> udp_endpoint_ptr;

      typedef std::shared_ptr<i2p::client::ClientDestination> destination_ptr;

      typedef i2p::data::IdentHash ident_hash_t;

      enum SocketType
      {
        eSocketTypeNull,
        eSocketTypeMaster,
        eSocketTypeStream,
        eSocketTypeSession,
        eSocketTypeAccept
      };

      enum SocketState
      {
        eSocketStateInit,
        eSocketStateHandshake,
        eSocketStateRunning,
        eSocketStateExit
      };

      struct SocketInfo
      {
        ident_hash_t remote_ident;
        tcp_endpoint_ptr local_tcp;
        tcp_endpoint_ptr remote_tcp;
        udp_endpoint_ptr local_udp;
        udp_endpoint_ptr remote_udp;
        SocketType sock_type;
      };

      struct SessionInfo
      {
        std::string ID;
        ident_hash_t local_ident;
        std::list<SocketInfo> sockets;
      };

      typedef std::shared_ptr<SessionInfo> SessionInfo_ptr;

      struct BridgeImpl;

      struct Bridge
      {
        Bridge(const std::string & address, uint16_t port);
        ~Bridge();
        void Start();
        void Stop();
        SessionInfo_ptr FindSession(const std::string & id) const;
        std::list<SessionInfo_ptr> ListSessions() const;
      private:
        BridgeImpl * m_Impl;
      };
    } // namespace sam

    typedef i2p::client::sam::Bridge SAMBridge;
  } // namespace client
} // namespace i2p

#endif
