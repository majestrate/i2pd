#include "NTCP2.h"
#include "NTCP2Session.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "NetDb.hpp"

namespace i2p
{
  namespace transport
  {

    const int NTCP2_CONNECT_TIMEOUT = 5;

    NTCP2Server::NTCP2Server() : 
      m_IsRunning(false), m_Thread(nullptr),
      m_Work(m_Service), m_TerminationTimer(m_Service),
      m_V4Acceptor(nullptr), m_V6Acceptor(nullptr)
    {

    }

    NTCP2Server::~NTCP2Server()
    {
      Stop();
    }

    void NTCP2Server::Start()
    {
      if(m_IsRunning)
        return;

      m_IsRunning = true;
      m_Thread = new std::thread(std::bind(&NTCP2Server::Run, this));

      auto & addresses = context.GetRouterInfo ().GetAddresses ();
      for (const auto & address : addresses)
      {
        if(!address) continue;
        if(address->transportStyle == i2p::data::RouterInfo::eTransportNTCP2)
        {
          if(address->host.is_v4())
          {
            try
            {
              m_V4Acceptor = new Acceptor_t(m_Service, Endpoint_t(boost::asio::ip::tcp::v4(), address->port));
            }
            catch ( std::exception & ex)
            {
              LogPrint(eLogError, "NTCP2: failed to bind to ipv4 port ", address->port, ": ", ex.what());
              continue;
            }

            LogPrint(eLogInfo, "NTCP2: Bound to TCP v4 port ", address->port);
            auto conn = std::make_shared<NTCP2Session>(*this);
            m_V4Acceptor->async_accept(conn->GetSocket(), std::bind(&NTCP2Server::HandleAccept, this, m_V4Acceptor, conn, std::placeholders::_1));
          } 
          else if (address->host.is_v6() && context.SupportsV6())
          {
            m_V6Acceptor = new Acceptor_t(m_Service);
            try
            {
              m_V6Acceptor->open(boost::asio::ip::tcp::v6());
              m_V6Acceptor->set_option(boost::asio::ip::v6_only (true));
              m_V6Acceptor->bind(Endpoint_t(boost::asio::ip::tcp::v6(), address->port));
              m_V6Acceptor->listen();
              LogPrint(eLogInfo, "NTCP2: Bound to TCP v6 port ", address->port);
              auto conn = std::make_shared<NTCP2Session>(*this);
              m_V6Acceptor->async_accept(conn->GetSocket(), std::bind(&NTCP2Server::HandleAccept, this, m_V6Acceptor, conn, std::placeholders::_1));
            }
            catch ( std::exception & ex)
            {
              LogPrint(eLogError, "NTCP2: failed to bind on v6 port ", address->port, ": ", ex.what());
              continue;
            }
          }
        }
        ScheduleTermination ();
      }
    }

    void NTCP2Server::Stop()
    {
      {
        auto sessions = m_Sessions;
        for(auto & itr : sessions)
          itr.second->Terminate();

        for(auto & itr : m_PendingIncomingSessions)
          itr->Terminate();
      }
      m_Sessions.clear();

      if(m_IsRunning)
      {
        m_IsRunning = false;
        m_TerminationTimer.cancel();
        if (m_V4Acceptor)
        {
          delete m_V4Acceptor;
          m_V4Acceptor = nullptr;
        }
        if (m_V6Acceptor)
        {
          delete m_V6Acceptor;
          m_V6Acceptor = nullptr;
        }
        m_Service.stop();
        if(m_Thread)
        {
          m_Thread->join();
          delete m_Thread;
          m_Thread = nullptr;
        }
      }
    }

    void NTCP2Server::Run ()
    {
      while(m_IsRunning)
      {
        try
        {
          m_Service.run ();
        }
        catch ( std::exception & ex)
        {
          LogPrint(eLogError, "NTCP2: runtime exception: ", ex.what());
        }
      }
    }

    bool NTCP2Server::AddSession(Session_ptr session)
    {
      if(!session || !session->GetRemoteIdentity()) return false;
      auto & ident = session->GetRemoteIdentity ()->GetIdentHash ();
      auto it  = m_Sessions.find(ident);
      if(it != m_Sessions.end ())
      {
        LogPrint(eLogWarning, "NTCP2: session to ", ident.ToBase64(), " aready exists");
        session->Terminate();
        return false;
      }
      m_Sessions.insert({ident, session});
      return true;
    }
    
    void NTCP2Server::RemoveSession(Session_ptr session)
    {
      if(session && session->GetRemoteIdentity ())
        m_Sessions.erase(session->GetRemoteIdentity()->GetIdentHash ());
    }

    NTCP2Server::Session_ptr NTCP2Server::FindSession(const Ident & ident)
    {
      auto it = m_Sessions.find(ident);
      if (it != m_Sessions.end ())
        return it->second;
      return nullptr;
    }

    void NTCP2Server::HandleAccept(Acceptor_t * a, Session_ptr conn, const error_t & error)
    {
      if(!error)
      {
        error_t ec;
        auto ep = conn->GetSocket().remote_endpoint(ec);
        if(!ec)
        {
          LogPrint(eLogDebug,"NTCP: inbound connection from ", ep);
          conn->ServerLogin();
          m_PendingIncomingSessions.push_back(conn);
        }
        else
          LogPrint(eLogError, "NTCP2: accept() error ", ec.message());
      }
      if(error != boost::asio::error::operation_aborted)
      {
        conn = std::make_shared<NTCP2Session>(*this);
        a->async_accept(conn->GetSocket(), std::bind(&NTCP2Server::HandleAccept, this, a, conn, std::placeholders::_1));
      }
    }

    void NTCP2Server::Connect(const IP_t & addr, uint16_t port, Session_ptr conn)
    {
      LogPrint(eLogDebug, "NTCP2: Connecting to ", addr, ":", port);
      m_Service.post([=]() {
        if(this->AddSession(conn))
        {
          auto timer = std::make_shared<Timer_t>(m_Service);
          timer->expires_from_now(boost::posix_time::seconds(NTCP2_CONNECT_TIMEOUT));
          timer->async_wait([conn](const error_t & ec) {
            if(ec != boost::asio::error::operation_aborted)
            {
              LogPrint(eLogInfo, "NTCP2: Not Connected in ", NTCP2_CONNECT_TIMEOUT, " seconds");
              conn->Terminate ();
            }
          });
          conn->GetSocket ().async_connect(Endpoint_t(addr, port), std::bind(&NTCP2Server::HandleConnect, this, std::placeholders::_1, conn, timer));
        }
      });
    }

    void NTCP2Server::HandleConnect(const error_t & ec, Session_ptr conn, Timer_ptr timer)
    {
      timer->cancel();
      if(ec)
      {
        LogPrint(eLogInfo, "NTCP2: Connect error ", ec.message ());
        if(ec != boost::asio::error::operation_aborted)
          i2p::data::netdb.SetUnreachable(conn->GetRemoteIdentity ()->GetIdentHash (), true);
        conn->Terminate ();
      }
      else
      {
        LogPrint(eLogDebug, "NTCP2: Connected to ", conn->GetSocket().remote_endpoint ());
        if(conn->GetSocket ().local_endpoint().protocol() == boost::asio::ip::tcp::v6())
          context.UpdateNTCP2V6Address(conn->GetSocket ().local_endpoint ().address ());
        conn->ClientLogin ();
      }
    }

  }
}