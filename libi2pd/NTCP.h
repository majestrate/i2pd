#ifndef NTCP_H
#define NTCP_H

namespace i2p
{
namespace transport
{
	class NTCPServer
	{
		public:

			enum RemoteAddressType
			{
				eIP4Address,
				eIP6Address,
				eHostname
			};

			enum ProxyType
			{
				eNoProxy,
				eSocksProxy,
				eHTTPProxy
			};


    NTCPServer ();
			~NTCPServer ();

			void Start ();
			void Stop ();

			bool AddNTCPSession (std::shared_ptr<NTCPSession> session);
			void RemoveNTCPSession (std::shared_ptr<NTCPSession> session);
			std::shared_ptr<NTCPSession> FindNTCPSession (const i2p::data::IdentHash& ident);
			void ConnectWithProxy (const std::string& addr, uint16_t port, RemoteAddressType addrtype, std::shared_ptr<NTCPSession> conn);
			void Connect(const boost::asio::ip::address & address, uint16_t port, std::shared_ptr<NTCPSession> conn);

			bool IsBoundV4() const { return m_NTCPAcceptor != nullptr; };
			bool IsBoundV6() const { return m_NTCPV6Acceptor != nullptr; };
			bool NetworkIsReady() const { return IsBoundV4() || IsBoundV6() ||	UsingProxy(); };
			bool UsingProxy() const { return m_ProxyType != eNoProxy; };

			void UseProxy(ProxyType proxy, const std::string & address, uint16_t port);

			boost::asio::io_service& GetService () { return m_Service; };

		private:

			void Run ();
			void HandleAccept (std::shared_ptr<NTCPSession> conn, const boost::system::error_code& error);
			void HandleAcceptV6 (std::shared_ptr<NTCPSession> conn, const boost::system::error_code& error);

			void HandleConnect (const boost::system::error_code& ecode, std::shared_ptr<NTCPSession> conn, std::shared_ptr<boost::asio::deadline_timer> timer);

			void HandleProxyConnect(const boost::system::error_code& ecode, std::shared_ptr<NTCPSession> conn, std::shared_ptr<boost::asio::deadline_timer> timer, const std::string & host, uint16_t port, RemoteAddressType adddrtype);
			void AfterSocksHandshake(std::shared_ptr<NTCPSession> conn, std::shared_ptr<boost::asio::deadline_timer> timer, const std::string & host, uint16_t port, RemoteAddressType adddrtype);

			// timer
			void ScheduleTermination ();
			void HandleTerminationTimer (const boost::system::error_code& ecode);

		private:
    i2p::Router * m_Router;
			bool m_IsRunning;
			std::thread * m_Thread;
			boost::asio::io_service m_Service;
			boost::asio::io_service::work m_Work;
			boost::asio::deadline_timer m_TerminationTimer;
			boost::asio::ip::tcp::acceptor * m_NTCPAcceptor, * m_NTCPV6Acceptor;
			std::map<i2p::data::IdentHash, std::shared_ptr<NTCPSession> > m_NTCPSessions; // access from m_Thread only
			std::list<std::shared_ptr<NTCPSession> > m_PendingIncomingSessions;

			ProxyType m_ProxyType;
			std::string m_ProxyAddress;
			uint16_t m_ProxyPort;
			boost::asio::ip::tcp::resolver m_Resolver;
			boost::asio::ip::tcp::endpoint * m_ProxyEndpoint;
		public:

			// for HTTP/I2PControl
			const decltype(m_NTCPSessions)& GetNTCPSessions () const { return m_NTCPSessions; };
	};
}
}

#endif
