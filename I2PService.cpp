#include "Destination.h"
#include "Identity.h"
#include "ClientContext.h"
#include "I2PService.h"


namespace i2p
{
namespace client
{
	static const i2p::data::SigningKeyType I2P_SERVICE_DEFAULT_KEY_TYPE = i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA256_P256;

	I2PService::I2PService (std::shared_ptr<ClientDestination> localDestination):
		m_LocalDestination (localDestination ? localDestination :
					i2p::client::context.CreateNewLocalDestination (false, I2P_SERVICE_DEFAULT_KEY_TYPE))
	{
	}
	
	I2PService::I2PService (i2p::data::SigningKeyType kt):
		m_LocalDestination (i2p::client::context.CreateNewLocalDestination (false, kt))
	{
	}
	
	void I2PService::CreateStream (StreamRequestComplete streamRequestComplete, const std::string& dest, int port) {
		assert(streamRequestComplete);
		i2p::data::IdentHash identHash;
		if (i2p::client::context.GetAddressBook ().GetIdentHash (dest, identHash))
			m_LocalDestination->CreateStream (streamRequestComplete, identHash, port);
		else
		{
			LogPrint (eLogWarning, "I2PService: Remote destination ", dest, " not found");
			streamRequestComplete (nullptr);
		}
	}

	TCPIPPipe::TCPIPPipe(I2PService * owner, std::shared_ptr<boost::asio::ip::tcp::socket> upstream, std::shared_ptr<boost::asio::ip::tcp::socket> downstream) : I2PServiceHandler(owner), m_up(upstream), m_down(downstream) {}

	TCPIPPipe::~TCPIPPipe()
	{
		Terminate();
	}
	
	void TCPIPPipe::Start()
	{
		AsyncReceiveUpstream();
		AsyncReceiveDownstream();
	}

	void TCPIPPipe::Terminate()
	{
		if(Kill()) return;
		Done(shared_from_this());
		if (m_up) {
			if (m_up->is_open()) {
				m_up->close();
			}
			m_up = nullptr;
		}
		if (m_down) {
			if (m_down->is_open()) {
				m_down->close();
			}
			m_down = nullptr;
		}
	}
	
	void TCPIPPipe::AsyncReceiveUpstream()
	{
		if (m_up) {
			m_up->async_read_some(boost::asio::buffer(m_upstream_to_down_buf, TCP_IP_PIPE_BUFFER_SIZE),
															std::bind(&TCPIPPipe::HandleUpstreamReceived, shared_from_this(),
																				std::placeholders::_1, std::placeholders::_2));
		} else {
			LogPrint(eLogError, "TCPIPPipe: no upstream socket for read");
		}
	}

	void TCPIPPipe::AsyncReceiveDownstream()
	{
		if (m_down) {
			m_down->async_read_some(boost::asio::buffer(m_downstream_to_up_buf, TCP_IP_PIPE_BUFFER_SIZE),
															std::bind(&TCPIPPipe::HandleDownstreamReceived, shared_from_this(),
																				std::placeholders::_1, std::placeholders::_2));
		} else {
			LogPrint(eLogError, "TCPIPPipe: no downstream socket for read");
		}
	}

	void TCPIPPipe::UpstreamWrite(const uint8_t * buf, size_t len)
	{
		if (m_up) {
			LogPrint(eLogDebug, "TCPIPPipe: write upstream ", (int)len);
			boost::asio::async_write(*m_up, boost::asio::buffer(buf, len),
															 boost::asio::transfer_all(),
															 std::bind(&TCPIPPipe::HandleUpstreamWrite,
																				 shared_from_this(),
																				 std::placeholders::_1)
															 );
		} else {
			LogPrint(eLogError, "tcpip pipe upstream socket null");
		}
	}

	void TCPIPPipe::DownstreamWrite(const uint8_t * buf, size_t len)
	{
		if (m_down) {
			LogPrint(eLogDebug, "TCPIPPipe: write downstream ", (int)len);
			boost::asio::async_write(*m_down, boost::asio::buffer(buf, len),
															 boost::asio::transfer_all(),
															 std::bind(&TCPIPPipe::HandleDownstreamWrite,
																				 shared_from_this(),
																				 std::placeholders::_1)
															 );
		} else { 
			LogPrint(eLogError, "tcpip pipe downstream socket null");
		}
	}
	
	
	void TCPIPPipe::HandleDownstreamReceived(const boost::system::error_code & ecode, std::size_t bytes_transfered)
	{
		LogPrint(eLogDebug, "TCPIPPipe downstream got ", (int) bytes_transfered);
		if (ecode) {
			LogPrint(eLogError, "TCPIPPipe Downstream read error:" , ecode.message());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate();
		} else {
			if (bytes_transfered > 0 ) {
				memcpy(m_upstream_buf, m_downstream_to_up_buf, bytes_transfered);
				UpstreamWrite(m_upstream_buf, bytes_transfered);
			}
			AsyncReceiveDownstream();
		}
	}

	void TCPIPPipe::HandleDownstreamWrite(const boost::system::error_code & ecode) {
		if (ecode) {
			LogPrint(eLogError, "TCPIPPipe Downstream write error:" , ecode.message());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate();
		}
	}
	
	void TCPIPPipe::HandleUpstreamWrite(const boost::system::error_code & ecode) {
		if (ecode) {
			LogPrint(eLogError, "TCPIPPipe Upstream write error:" , ecode.message());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate();
		}
	}
	
	void TCPIPPipe::HandleUpstreamReceived(const boost::system::error_code & ecode, std::size_t bytes_transfered)
	{
		LogPrint(eLogDebug, "TCPIPPipe upstream got ", (int) bytes_transfered);
		if (ecode) {
			LogPrint(eLogError, "TCPIPPipe Upstream read error:" , ecode.message());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate();
		} else {
			if (bytes_transfered > 0 ) {
				memcpy(m_upstream_buf, m_upstream_to_down_buf, bytes_transfered);
				DownstreamWrite(m_upstream_buf, bytes_transfered);
			}
			AsyncReceiveUpstream();
		}
	}
	
	void TCPIPAcceptor::Start ()
	{
		m_Acceptor.listen ();
		Accept ();
	}

	void TCPIPAcceptor::Stop ()
	{
		m_Acceptor.close();
		m_Timer.cancel ();
		ClearHandlers();
	}

	void TCPIPAcceptor::Accept ()
	{
		auto newSocket = std::make_shared<boost::asio::ip::tcp::socket> (GetService ());
		m_Acceptor.async_accept (*newSocket, std::bind (&TCPIPAcceptor::HandleAccept, this,
			std::placeholders::_1, newSocket));
	}

	void TCPIPAcceptor::HandleAccept (const boost::system::error_code& ecode, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		if (!ecode)
		{
			LogPrint(eLogDebug, "I2PService: ", GetName(), " accepted");
			auto handler = CreateHandler(socket);
			if (handler) 
			{
				AddHandler(handler);
				handler->Handle();
			} 
			else 
				socket->close();
			Accept();
		}
		else
		{
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogError, "I2PService: ", GetName(), " closing socket on accept because: ", ecode.message ());
		}
	}

}
}
