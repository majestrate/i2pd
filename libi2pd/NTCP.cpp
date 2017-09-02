#include "NTCP.h"
namespace i2p
{
namespace transport
{
	NTCPServer::NTCPServer(Transports & parent):
		m_Parent(parent),
		m_IsRunning (false), m_Thread (nullptr), m_Work (m_Service),
		m_TerminationTimer (m_Service), m_NTCPAcceptor (nullptr), m_NTCPV6Acceptor (nullptr),
		m_ProxyType(eNoProxy), m_Resolver(m_Service), m_ProxyEndpoint(nullptr)
	{
	}

	NTCPServer::~NTCPServer ()
	{
		Stop ();
	}

	void NTCPServer::Start ()
	{
		if (!m_IsRunning)
			{
				m_IsRunning = true;
				m_Thread = new std::thread (std::bind (&NTCPServer::Run, this));
				// we are using a proxy, don't create any acceptors
				if(UsingProxy())
					{
						// TODO: resolve proxy until it is resolved
						boost::asio::ip::tcp::resolver::query q(m_ProxyAddress, std::to_string(m_ProxyPort));
						boost::system::error_code e;
						auto itr = m_Resolver.resolve(q, e);
						if(e)
							{
								LogPrint(eLogError, "NTCP: Failed to resolve proxy ", e.message());
							}
						else
							{
								m_ProxyEndpoint = new boost::asio::ip::tcp::endpoint(*itr);
							}
					}
				else
					{
						bool fakehttps; i2p::config::GetOption("fakehttps", fakehttps);
						// create acceptors
						auto& addresses = context.GetRouterInfo ().GetAddresses ();
						for (const auto& address: addresses)
							{
								if (!address) continue;
								if (address->transportStyle == i2p::data::RouterInfo::eTransportNTCP)
									{

										if (fakehttps)
											{
												i2p::config::GetOption("port", address->port);
											}

										if (address->host.is_v4())
											{

												try
													{
														m_NTCPAcceptor = new boost::asio::ip::tcp::acceptor (m_Service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), address->port));
													} catch ( std::exception & ex ) {
													/** fail to bind ip4 */
													LogPrint(eLogError, "NTCP: Failed to bind to ip4 port ",address->port, ex.what());
													continue;
												}

												LogPrint (eLogInfo, "NTCP: Start listening TCP port ", address->port);
												auto conn = std::make_shared<NTCPSession>(*this);
												m_NTCPAcceptor->async_accept(conn->GetSocket (), std::bind (&NTCPServer::HandleAccept, this, conn, std::placeholders::_1));
											}
										else if (address->host.is_v6() && context.SupportsV6 ())
											{
												m_NTCPV6Acceptor = new boost::asio::ip::tcp::acceptor (m_Service);
												try
													{
														m_NTCPV6Acceptor->open (boost::asio::ip::tcp::v6());
														m_NTCPV6Acceptor->set_option (boost::asio::ip::v6_only (true));
														m_NTCPV6Acceptor->bind (boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v6(), address->port));
														m_NTCPV6Acceptor->listen ();

														LogPrint (eLogInfo, "NTCP: Start listening V6 TCP port ", address->port);
														auto conn = std::make_shared<NTCPSession> (*this);
														m_NTCPV6Acceptor->async_accept(conn->GetSocket (), std::bind (&NTCPServer::HandleAcceptV6, this, conn, std::placeholders::_1));
													} catch ( std::exception & ex ) {
													LogPrint(eLogError, "NTCP: failed to bind to ip6 port ", address->port);
													continue;
												}
											}
									}
							}
					}
				ScheduleTermination ();
			}
	}

	void NTCPServer::Stop ()
	{
		{
			// we have to copy it because Terminate changes m_NTCPSessions
			auto ntcpSessions = m_NTCPSessions;
			for (auto& it: ntcpSessions)
				it.second->Terminate ();
			for (auto& it: m_PendingIncomingSessions)
				it->Terminate ();
		}
		m_NTCPSessions.clear ();

		if (m_IsRunning)
			{
				m_IsRunning = false;
				m_TerminationTimer.cancel ();
				if (m_NTCPAcceptor)
					{
						delete m_NTCPAcceptor;
						m_NTCPAcceptor = nullptr;
					}
				if (m_NTCPV6Acceptor)
					{
						delete m_NTCPV6Acceptor;
						m_NTCPV6Acceptor = nullptr;
					}
				m_Service.stop ();
				if (m_Thread)
					{
						m_Thread->join ();
						delete m_Thread;
						m_Thread = nullptr;
					}
				if(m_ProxyEndpoint)
					{
						delete m_ProxyEndpoint;
						m_ProxyEndpoint = nullptr;
					}
			}
	}


	void NTCPServer::Run ()
	{
		while (m_IsRunning)
			{
				try
					{
						m_Service.run ();
					}
				catch (std::exception& ex)
					{
						LogPrint (eLogError, "NTCP: runtime exception: ", ex.what ());
					}
			}
	}

	bool NTCPServer::AddNTCPSession (std::shared_ptr<NTCPSession> session)
	{
		if (!session || !session->GetRemoteIdentity ()) return false;
		auto& ident = session->GetRemoteIdentity ()->GetIdentHash ();
		auto it = m_NTCPSessions.find (ident);
		if (it != m_NTCPSessions.end ())
			{
				LogPrint (eLogWarning, "NTCP: session to ", ident.ToBase64 (), " already exists");
				session->Terminate();
				return false;
			}
		m_NTCPSessions.insert (std::pair<i2p::data::IdentHash, std::shared_ptr<NTCPSession> >(ident, session));
		return true;
	}

	void NTCPServer::RemoveNTCPSession (std::shared_ptr<NTCPSession> session)
	{
		if (session && session->GetRemoteIdentity ())
			m_NTCPSessions.erase (session->GetRemoteIdentity ()->GetIdentHash ());
	}

	std::shared_ptr<NTCPSession> NTCPServer::FindNTCPSession (const i2p::data::IdentHash& ident)
	{
		auto it = m_NTCPSessions.find (ident);
		if (it != m_NTCPSessions.end ())
			return it->second;
		return nullptr;
	}

	void NTCPServer::HandleAccept (std::shared_ptr<NTCPSession> conn, const boost::system::error_code& error)
	{
		if (!error)
			{
				boost::system::error_code ec;
				auto ep = conn->GetSocket ().remote_endpoint(ec);
				if (!ec)
					{
						LogPrint (eLogDebug, "NTCP: Connected from ", ep);
						if (conn)
							{
								conn->ServerLogin ();
								m_PendingIncomingSessions.push_back (conn);
							}
					}
				else
					LogPrint (eLogError, "NTCP: Connected from error ", ec.message ());
			}


		if (error != boost::asio::error::operation_aborted)
			{
				conn = std::make_shared<NTCPSession> (*this);
				m_NTCPAcceptor->async_accept(conn->GetSocket (), std::bind (&NTCPServer::HandleAccept, this,
																																		conn, std::placeholders::_1));
			}
	}

	void NTCPServer::HandleAcceptV6 (std::shared_ptr<NTCPSession> conn, const boost::system::error_code& error)
	{
		if (!error)
			{
				boost::system::error_code ec;
				auto ep = conn->GetSocket ().remote_endpoint(ec);
				if (!ec)
					{
						LogPrint (eLogDebug, "NTCP: Connected from ", ep);
						if (conn)
							{
								conn->ServerLogin ();
								m_PendingIncomingSessions.push_back (conn);
							}
					}
				else
					LogPrint (eLogError, "NTCP: Connected from error ", ec.message ());
			}

		if (error != boost::asio::error::operation_aborted)
			{
				conn = std::make_shared<NTCPSession> (*this);
				m_NTCPV6Acceptor->async_accept(conn->GetSocket (), std::bind (&NTCPServer::HandleAcceptV6, this,
																																			conn, std::placeholders::_1));
			}
	}

	void NTCPServer::Connect(const boost::asio::ip::address & address, uint16_t port, std::shared_ptr<NTCPSession> conn)
	{
		LogPrint (eLogDebug, "NTCP: Connecting to ", address ,":",  port);
		m_Service.post([=]() {
				if (this->AddNTCPSession (conn))
					{

						auto timer = std::make_shared<boost::asio::deadline_timer>(m_Service);
						timer->expires_from_now (boost::posix_time::seconds(NTCP_CONNECT_TIMEOUT));
						timer->async_wait ([conn](const boost::system::error_code& ecode) {
								if (ecode != boost::asio::error::operation_aborted)
									{
										LogPrint (eLogInfo, "NTCP: Not connected in ", NTCP_CONNECT_TIMEOUT, " seconds");
										conn->Terminate ();
									}
							});
						conn->GetSocket ().async_connect (boost::asio::ip::tcp::endpoint (address, port), std::bind (&NTCPServer::HandleConnect, this, std::placeholders::_1, conn, timer));
					}
			});
	}

	void NTCPServer::ConnectWithProxy (const std::string& host, uint16_t port, RemoteAddressType addrtype, std::shared_ptr<NTCPSession> conn)
	{
		if(m_ProxyEndpoint == nullptr)
			{
				return;
			}
		m_Service.post([=]() {
				if (this->AddNTCPSession (conn))
					{

						auto timer = std::make_shared<boost::asio::deadline_timer>(m_Service);
						auto timeout = NTCP_CONNECT_TIMEOUT * 5;
						conn->SetTerminationTimeout(timeout * 2);
						timer->expires_from_now (boost::posix_time::seconds(timeout));
						timer->async_wait ([conn, timeout](const boost::system::error_code& ecode) {
								if (ecode != boost::asio::error::operation_aborted)
									{
										LogPrint (eLogInfo, "NTCP: Not connected in ", timeout, " seconds");
										i2p::data::netdb.SetUnreachable (conn->GetRemoteIdentity ()->GetIdentHash (), true);
										conn->Terminate ();
									}
							});
						conn->GetSocket ().async_connect (*m_ProxyEndpoint, std::bind (&NTCPServer::HandleProxyConnect, this, std::placeholders::_1, conn, timer, host, port, addrtype));
					}
			});
	}

	void NTCPServer::HandleConnect (const boost::system::error_code& ecode, std::shared_ptr<NTCPSession> conn, std::shared_ptr<boost::asio::deadline_timer> timer)
	{
		timer->cancel ();
		if (ecode)
			{
				LogPrint (eLogInfo, "NTCP: Connect error ", ecode.message ());
				if (ecode != boost::asio::error::operation_aborted)
					i2p::data::netdb.SetUnreachable (conn->GetRemoteIdentity ()->GetIdentHash (), true);
				conn->Terminate ();
			}
		else
			{
				LogPrint (eLogDebug, "NTCP: Connected to ", conn->GetSocket ().remote_endpoint ());
				if (conn->GetSocket ().local_endpoint ().protocol () == boost::asio::ip::tcp::v6()) // ipv6
					context.UpdateNTCPV6Address (conn->GetSocket ().local_endpoint ().address ());
				conn->ClientLogin ();
			}
	}

	void NTCPServer::UseProxy(ProxyType proxytype, const std::string & addr, uint16_t port)
	{
		m_ProxyType = proxytype;
		m_ProxyAddress = addr;
		m_ProxyPort = port;
	}

	void NTCPServer::HandleProxyConnect(const boost::system::error_code& ecode, std::shared_ptr<NTCPSession> conn, std::shared_ptr<boost::asio::deadline_timer> timer, const std::string & host, uint16_t port, RemoteAddressType addrtype)
	{
		if(ecode)
			{
				LogPrint(eLogWarning, "NTCP: failed to connect to proxy ", ecode.message());
				timer->cancel();
				conn->Terminate();
				return;
			}
		if(m_ProxyType == eSocksProxy)
			{
				// TODO: support username/password auth etc
				uint8_t buff[3] = {0x05, 0x01, 0x00};
				boost::asio::async_write(conn->GetSocket(), boost::asio::buffer(buff, 3), boost::asio::transfer_all(), [=] (const boost::system::error_code & ec, std::size_t transferred) {
						(void) transferred;
						if(ec)
							{
								LogPrint(eLogWarning, "NTCP: socks5 write error ", ec.message());
							}
					});
				uint8_t readbuff[2];
				boost::asio::async_read(conn->GetSocket(), boost::asio::buffer(readbuff, 2), [=](const boost::system::error_code & ec, std::size_t transferred) {
						if(ec)
							{
								LogPrint(eLogError, "NTCP: socks5 read error ", ec.message());
								timer->cancel();
								conn->Terminate();
								return;
							}
						else if(transferred == 2)
							{
								if(readbuff[1] == 0x00)
									{
										AfterSocksHandshake(conn, timer, host, port, addrtype);
										return;
									}
								else if (readbuff[1] == 0xff)
									{
										LogPrint(eLogError, "NTCP: socks5 proxy rejected authentication");
										timer->cancel();
										conn->Terminate();
										return;
									}
							}
						LogPrint(eLogError, "NTCP: socks5 server gave invalid response");
						timer->cancel();
						conn->Terminate();
					});
			}
		else if(m_ProxyType == eHTTPProxy)
			{
				i2p::http::HTTPReq req;
				req.method = "CONNECT";
				req.version ="HTTP/1.1";
				if(addrtype == eIP6Address)
					req.uri = "[" + host + "]:" + std::to_string(port);
				else
					req.uri = host + ":" + std::to_string(port);

				boost::asio::streambuf writebuff;
				std::ostream out(&writebuff);
				out << req.to_string();

				boost::asio::async_write(conn->GetSocket(), writebuff.data(), boost::asio::transfer_all(), [=](const boost::system::error_code & ec, std::size_t transferred) {
						(void) transferred;
						if(ec)
							LogPrint(eLogError, "NTCP: http proxy write error ", ec.message());
					});

				boost::asio::streambuf * readbuff = new boost::asio::streambuf;
				boost::asio::async_read_until(conn->GetSocket(), *readbuff, "\r\n\r\n", [=] (const boost::system::error_code & ec, std::size_t transferred) {
						if(ec)
							{
								LogPrint(eLogError, "NTCP: http proxy read error ", ec.message());
								timer->cancel();
								conn->Terminate();
							}
						else
							{
								readbuff->commit(transferred);
								i2p::http::HTTPRes res;
								if(res.parse(boost::asio::buffer_cast<const char*>(readbuff->data()), readbuff->size()) > 0)
									{
										if(res.code == 200)
											{
												timer->cancel();
												conn->ClientLogin();
												delete readbuff;
												return;
											}
										else
											{
												LogPrint(eLogError, "NTCP: http proxy rejected request ", res.code);
											}
									}
								else
									LogPrint(eLogError, "NTCP: http proxy gave malformed response");
								timer->cancel();
								conn->Terminate();
								delete readbuff;
							}
					});
			}
		else
			LogPrint(eLogError, "NTCP: unknown proxy type, invalid state");
	}

	void NTCPServer::AfterSocksHandshake(std::shared_ptr<NTCPSession> conn, std::shared_ptr<boost::asio::deadline_timer> timer, const std::string & host, uint16_t port, RemoteAddressType addrtype)
	{

		// build request
		size_t sz = 0;
		uint8_t buff[256];
		uint8_t readbuff[256];
		buff[0] = 0x05;
		buff[1] = 0x01;
		buff[2] = 0x00;

		if(addrtype == eIP4Address)
			{
				buff[3] = 0x01;
				auto addr = boost::asio::ip::address::from_string(host).to_v4();
				auto addrbytes = addr.to_bytes();
				auto addrsize = addrbytes.size();
				memcpy(buff+4, addrbytes.data(), addrsize);
			}
		else if (addrtype == eIP6Address)
			{
				buff[3] = 0x04;
				auto addr = boost::asio::ip::address::from_string(host).to_v6();
				auto addrbytes = addr.to_bytes();
				auto addrsize = addrbytes.size();
				memcpy(buff+4, addrbytes.data(), addrsize);
			}
		else if (addrtype == eHostname)
			{
				buff[3] = 0x03;
				size_t addrsize = host.size();
				sz = addrsize + 1 + 4;
				if (2 + sz > sizeof(buff))
					{
						// too big
						return;
					}
				buff[4] = (uint8_t) addrsize;
				memcpy(buff+4, host.c_str(), addrsize);
			}
		htobe16buf(buff+sz, port);
		sz += 2;
		boost::asio::async_write(conn->GetSocket(), boost::asio::buffer(buff, sz), boost::asio::transfer_all(), [=](const boost::system::error_code & ec, std::size_t written) {
				if(ec)
					{
						LogPrint(eLogError, "NTCP: failed to write handshake to socks proxy ", ec.message());
						return;
					}
			});

		boost::asio::async_read(conn->GetSocket(), boost::asio::buffer(readbuff, sz), [=](const boost::system::error_code & e, std::size_t transferred) {
				if(e)
					{
						LogPrint(eLogError, "NTCP: socks proxy read error ", e.message());
					}
				else if(transferred == sz)
					{
						if( readbuff[1] == 0x00)
							{
								timer->cancel();
								conn->ClientLogin();
								return;
							}
					}
				if(!e)
					i2p::data::netdb.SetUnreachable (conn->GetRemoteIdentity ()->GetIdentHash (), true);
				timer->cancel();
				conn->Terminate();
			});
	}

	void NTCPServer::ScheduleTermination ()
	{
		m_TerminationTimer.expires_from_now (boost::posix_time::seconds(NTCP_TERMINATION_CHECK_TIMEOUT));
		m_TerminationTimer.async_wait (std::bind (&NTCPServer::HandleTerminationTimer,
																							this, std::placeholders::_1));
	}

	void NTCPServer::HandleTerminationTimer (const boost::system::error_code& ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
			{
				auto ts = i2p::util::GetSecondsSinceEpoch ();
				// established
				for (auto& it: m_NTCPSessions)
					if (it.second->IsTerminationTimeoutExpired (ts))
						{
							auto session = it.second;
							// Termniate modifies m_NTCPSession, so we postpone it
							m_Service.post ([session] {
									LogPrint (eLogDebug, "NTCP: No activity for ", session->GetTerminationTimeout (), " seconds");
									session->Terminate ();
								});
						}
				// pending
				for (auto it = m_PendingIncomingSessions.begin (); it != m_PendingIncomingSessions.end ();)
					{
						if ((*it)->IsEstablished () || (*it)->IsTerminated ())
							it = m_PendingIncomingSessions.erase (it); // established or terminated
						else if ((*it)->IsTerminationTimeoutExpired (ts))
							{
								(*it)->Terminate ();
								it = m_PendingIncomingSessions.erase (it); // expired
							}
						else
							it++;
					}

				ScheduleTermination ();
			}
	}
}
}
