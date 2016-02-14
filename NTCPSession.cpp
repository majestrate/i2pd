#include <string.h>
#include <stdlib.h>
#include <openssl/dh.h>
#include <openssl/sha.h>
#include <zlib.h>
#include "I2PEndian.h"
#include "Base.h"
#include "Log.h"
#include "Timestamp.h"
#include "Crypto.h"
#include "I2NPProtocol.h"
#include "RouterContext.h"
#include "Transports.h"
#include "NetDb.h"
#include "NTCPSession.h"

using namespace i2p::crypto;

namespace i2p
{
namespace transport
{
	NTCPSession::NTCPSession (NTCPServer& server, std::shared_ptr<const i2p::data::RouterInfo> in_RemoteRouter): 
		TransportSession (in_RemoteRouter),	m_Server (server), m_Socket (m_Server.GetService ()), 
		m_TerminationTimer (m_Server.GetService ()), m_IsEstablished (false), m_IsTerminated (false),
		m_ReceiveBufferOffset (0), m_NextMessage (nullptr), m_IsSending (false)
	{		
		m_DHKeysPair = transports.GetNextDHKeysPair ();
		m_Establisher = new Establisher;
	}
	
	NTCPSession::~NTCPSession ()
	{
		delete m_Establisher;
	}

	void NTCPSession::CreateAESKey (uint8_t * pubKey, i2p::crypto::AESKey& key)
	{
		uint8_t sharedKey[256];
		m_DHKeysPair->Agree (pubKey, sharedKey);
		
		uint8_t * aesKey = key;
		if (sharedKey[0] & 0x80)
		{
			aesKey[0] = 0;
			memcpy (aesKey + 1, sharedKey, 31);
		}	
		else if (sharedKey[0])	
			memcpy (aesKey, sharedKey, 32);
		else
		{
			// find first non-zero byte
			uint8_t * nonZero = sharedKey + 1;
			while (!*nonZero)
			{
				nonZero++;
				if (nonZero - sharedKey > 32)
				{
					LogPrint (eLogWarning, "NTCP: First 32 bytes of shared key is all zeros, ignored");
					return;
				}	
			}
			memcpy (aesKey, nonZero, 32);
		}
	}	

	void NTCPSession::Done ()
	{
		m_Server.GetService ().post (std::bind (&NTCPSession::Terminate, shared_from_this ()));  
	}	
		
	void NTCPSession::Terminate ()
	{
		if (!m_IsTerminated)
		{	
			m_IsTerminated = true;
			m_IsEstablished = false;
			m_Socket.close ();
			transports.PeerDisconnected (shared_from_this ());
			m_Server.RemoveNTCPSession (shared_from_this ());
			m_SendQueue.clear ();
			m_NextMessage = nullptr;
			m_TerminationTimer.cancel ();
			LogPrint (eLogDebug, "NTCP: session terminated");
		}	
	}	

	void NTCPSession::Connected ()
	{
		m_IsEstablished = true;

		delete m_Establisher;
		m_Establisher = nullptr;
		
		m_DHKeysPair = nullptr;	

		SendTimeSyncMessage ();
		m_SendQueue.push_back (CreateDatabaseStoreMsg ()); // we tell immediately who we are		

		transports.PeerConnected (shared_from_this ());
	}	
		
	void NTCPSession::ClientLogin ()
	{
		if (!m_DHKeysPair)
			m_DHKeysPair = transports.GetNextDHKeysPair ();
		// send Phase1
		const uint8_t * x = m_DHKeysPair->GetPublicKey ();
		memcpy (m_Establisher->phase1.pubKey, x, 256);
		SHA256(x, 256, m_Establisher->phase1.HXxorHI);
		const uint8_t * ident = m_RemoteIdentity->GetIdentHash ();
		for (int i = 0; i < 32; i++)
			m_Establisher->phase1.HXxorHI[i] ^= ident[i];
		
		boost::asio::async_write (m_Socket, boost::asio::buffer (&m_Establisher->phase1, sizeof (NTCPPhase1)), boost::asio::transfer_all (),
        	std::bind(&NTCPSession::HandlePhase1Sent, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
		ScheduleTermination ();
	}	

	void NTCPSession::ServerLogin ()
	{
		boost::system::error_code ec;
		auto ep = m_Socket.remote_endpoint(ec);	
		if (!ec)
		{	
			m_ConnectedFrom = ep.address ();
			// receive Phase1
			boost::asio::async_read (m_Socket, boost::asio::buffer(&m_Establisher->phase1, sizeof (NTCPPhase1)), boost::asio::transfer_all (),                    
				std::bind(&NTCPSession::HandlePhase1Received, shared_from_this (), 
					std::placeholders::_1, std::placeholders::_2));
			ScheduleTermination ();	
		}
	}	
		
	void NTCPSession::HandlePhase1Sent (const boost::system::error_code& ecode, std::size_t bytes_transferred)
	{
		(void) bytes_transferred;
		if (ecode)
        {
			LogPrint (eLogInfo, "NTCP: couldn't send Phase 1 message: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
		{	
			boost::asio::async_read (m_Socket, boost::asio::buffer(&m_Establisher->phase2, sizeof (NTCPPhase2)), boost::asio::transfer_all (),                 
				std::bind(&NTCPSession::HandlePhase2Received, shared_from_this (), 
					std::placeholders::_1, std::placeholders::_2));
		}	
	}	

	void NTCPSession::HandlePhase1Received (const boost::system::error_code& ecode, std::size_t bytes_transferred)
	{
		(void) bytes_transferred;
		if (ecode)
        {
			LogPrint (eLogInfo, "NTCP: phase 1 read error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
		{	
			// verify ident
			uint8_t digest[32];
			SHA256(m_Establisher->phase1.pubKey, 256, digest);
			const uint8_t * ident = i2p::context.GetIdentHash ();
			for (int i = 0; i < 32; i++)
			{	
				if ((m_Establisher->phase1.HXxorHI[i] ^ ident[i]) != digest[i])
				{
					LogPrint (eLogError, "NTCP: phase 1 error: ident mismatch");
					Terminate ();
					return;
				}	
			}	
			
			SendPhase2 ();
		}	
	}	

	void NTCPSession::SendPhase2 ()
	{
		if (!m_DHKeysPair)
			m_DHKeysPair = transports.GetNextDHKeysPair ();
		const uint8_t * y = m_DHKeysPair->GetPublicKey ();
		memcpy (m_Establisher->phase2.pubKey, y, 256);
		uint8_t xy[512];
		memcpy (xy, m_Establisher->phase1.pubKey, 256);
		memcpy (xy + 256, y, 256);
		SHA256(xy, 512, m_Establisher->phase2.encrypted.hxy); 
		uint32_t tsB = htobe32 (i2p::util::GetSecondsSinceEpoch ());
		memcpy (m_Establisher->phase2.encrypted.timestamp, &tsB, 4);
		RAND_bytes (m_Establisher->phase2.encrypted.filler, 12);

		i2p::crypto::AESKey aesKey;
		CreateAESKey (m_Establisher->phase1.pubKey, aesKey);
		m_Encryption.SetKey (aesKey);
		m_Encryption.SetIV (y + 240);
		m_Decryption.SetKey (aesKey);
		m_Decryption.SetIV (m_Establisher->phase1.HXxorHI + 16);
		
		m_Encryption.Encrypt ((uint8_t *)&m_Establisher->phase2.encrypted, sizeof(m_Establisher->phase2.encrypted), (uint8_t *)&m_Establisher->phase2.encrypted);
		boost::asio::async_write (m_Socket, boost::asio::buffer (&m_Establisher->phase2, sizeof (NTCPPhase2)), boost::asio::transfer_all (),
        	std::bind(&NTCPSession::HandlePhase2Sent, shared_from_this (), std::placeholders::_1, std::placeholders::_2, tsB));

	}	
		
	void NTCPSession::HandlePhase2Sent (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsB)
	{
		(void) bytes_transferred;
		if (ecode)
        {
			LogPrint (eLogInfo, "NTCP: Couldn't send Phase 2 message: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
		{	
			boost::asio::async_read (m_Socket, boost::asio::buffer(m_ReceiveBuffer, NTCP_DEFAULT_PHASE3_SIZE), boost::asio::transfer_all (),                   
				std::bind(&NTCPSession::HandlePhase3Received, shared_from_this (), 
					std::placeholders::_1, std::placeholders::_2, tsB));
		}	
	}	
		
	void NTCPSession::HandlePhase2Received (const boost::system::error_code& ecode, std::size_t bytes_transferred)
	{
		(void) bytes_transferred;
		if (ecode)
        {
			LogPrint (eLogInfo, "NTCP: Phase 2 read error: ", ecode.message (), ". Wrong ident assumed");
			if (ecode != boost::asio::error::operation_aborted)
			{
				// this RI is not valid
				i2p::data::netdb.SetUnreachable (GetRemoteIdentity ()->GetIdentHash (), true);
				transports.ReuseDHKeysPair (m_DHKeysPair);
				m_DHKeysPair = nullptr;
				Terminate ();
			}
		}
		else
		{	
			i2p::crypto::AESKey aesKey;
			CreateAESKey (m_Establisher->phase2.pubKey, aesKey);
			m_Decryption.SetKey (aesKey);
			m_Decryption.SetIV (m_Establisher->phase2.pubKey + 240);
			m_Encryption.SetKey (aesKey);
			m_Encryption.SetIV (m_Establisher->phase1.HXxorHI + 16);
			
			m_Decryption.Decrypt((uint8_t *)&m_Establisher->phase2.encrypted, sizeof(m_Establisher->phase2.encrypted), (uint8_t *)&m_Establisher->phase2.encrypted);
			// verify
			uint8_t xy[512];
			memcpy (xy, m_DHKeysPair->GetPublicKey (), 256);
			memcpy (xy + 256, m_Establisher->phase2.pubKey, 256);
			uint8_t digest[32];
			SHA256 (xy, 512, digest);
			if (memcmp(m_Establisher->phase2.encrypted.hxy, digest, 32)) 
			{
				LogPrint (eLogError, "NTCP: Phase 2 process error: incorrect hash");
				transports.ReuseDHKeysPair (m_DHKeysPair);
				m_DHKeysPair = nullptr;
				Terminate ();
				return ;
			}	
			SendPhase3 ();
		}	
	}	

	void NTCPSession::SendPhase3 ()
	{
		auto keys = i2p::context.GetPrivateKeys ();
		uint8_t * buf = m_ReceiveBuffer; 
		htobe16buf (buf, keys.GetPublic ()->GetFullLen ());
		buf += 2;
		buf += i2p::context.GetIdentity ()->ToBuffer (buf, NTCP_BUFFER_SIZE);
		uint32_t tsA = htobe32 (i2p::util::GetSecondsSinceEpoch ());
		htobuf32(buf,tsA);
		buf += 4;		
		size_t signatureLen = keys.GetPublic ()->GetSignatureLen ();
		size_t len = (buf - m_ReceiveBuffer) + signatureLen;
		size_t paddingSize = len & 0x0F; // %16
		if (paddingSize > 0) 
		{
			paddingSize = 16 - paddingSize;
			// fill padding with random data
			RAND_bytes(buf, paddingSize);
			buf += paddingSize;
			len += paddingSize;
		}

		SignedData s;
		s.Insert (m_Establisher->phase1.pubKey, 256); // x
		s.Insert (m_Establisher->phase2.pubKey, 256); // y
		s.Insert (m_RemoteIdentity->GetIdentHash (), 32); // ident
 		s.Insert (tsA);	// tsA
		s.Insert (m_Establisher->phase2.encrypted.timestamp, 4); // tsB
		s.Sign (keys, buf);

		m_Encryption.Encrypt(m_ReceiveBuffer, len, m_ReceiveBuffer);		        
		boost::asio::async_write (m_Socket, boost::asio::buffer (m_ReceiveBuffer, len), boost::asio::transfer_all (),
        	std::bind(&NTCPSession::HandlePhase3Sent, shared_from_this (), std::placeholders::_1, std::placeholders::_2, tsA));				
	}	
		
	void NTCPSession::HandlePhase3Sent (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsA)
	{
		(void) bytes_transferred; 
		if (ecode)
        {
			LogPrint (eLogInfo, "NTCP: Couldn't send Phase 3 message: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
		{	
			// wait for phase4 
			auto signatureLen = m_RemoteIdentity->GetSignatureLen ();
			size_t paddingSize = signatureLen & 0x0F; // %16
			if (paddingSize > 0) signatureLen += (16 - paddingSize);	
			boost::asio::async_read (m_Socket, boost::asio::buffer(m_ReceiveBuffer, signatureLen), boost::asio::transfer_all (),                  
				std::bind(&NTCPSession::HandlePhase4Received, shared_from_this (), 
					std::placeholders::_1, std::placeholders::_2, tsA));
		}	
	}	

	void NTCPSession::HandlePhase3Received (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsB)
	{	
		if (ecode)
        {
			LogPrint (eLogInfo, "NTCP: Phase 3 read error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
		{	
			m_Decryption.Decrypt (m_ReceiveBuffer, bytes_transferred, m_ReceiveBuffer);
			uint8_t * buf = m_ReceiveBuffer;
			uint16_t size = bufbe16toh (buf);
			SetRemoteIdentity (std::make_shared<i2p::data::IdentityEx> (buf + 2, size));
			if (m_Server.FindNTCPSession (m_RemoteIdentity->GetIdentHash ()))
			{
				LogPrint (eLogInfo, "NTCP: session already exists");
				Terminate ();
			}	
			size_t expectedSize = size + 2/*size*/ + 4/*timestamp*/ + m_RemoteIdentity->GetSignatureLen ();
			size_t paddingLen = expectedSize & 0x0F;
			if (paddingLen) paddingLen = (16 - paddingLen);	
			if (expectedSize > NTCP_DEFAULT_PHASE3_SIZE)
			{
				// we need more bytes for Phase3
				expectedSize += paddingLen;	
				boost::asio::async_read (m_Socket, boost::asio::buffer(m_ReceiveBuffer + NTCP_DEFAULT_PHASE3_SIZE, expectedSize - NTCP_DEFAULT_PHASE3_SIZE), boost::asio::transfer_all (),                   
				std::bind(&NTCPSession::HandlePhase3ExtraReceived, shared_from_this (), 
					std::placeholders::_1, std::placeholders::_2, tsB, paddingLen));
			}
			else
				HandlePhase3 (tsB, paddingLen);
		}	
	}

	void NTCPSession::HandlePhase3ExtraReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsB, size_t paddingLen)
	{
		if (ecode)
        {
			LogPrint (eLogInfo, "NTCP: Phase 3 extra read error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
		{
			m_Decryption.Decrypt (m_ReceiveBuffer + NTCP_DEFAULT_PHASE3_SIZE, bytes_transferred, m_ReceiveBuffer+ NTCP_DEFAULT_PHASE3_SIZE);
			HandlePhase3 (tsB, paddingLen);
		}		
	}	

	void NTCPSession::HandlePhase3 (uint32_t tsB, size_t paddingLen)
	{
		uint8_t * buf = m_ReceiveBuffer + m_RemoteIdentity->GetFullLen () + 2 /*size*/;
		uint32_t tsA = buf32toh(buf); 
		buf += 4;
		buf += paddingLen;	

		// check timestamp
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		uint32_t tsA1 = be32toh (tsA);
		if (tsA1 < ts - NTCP_CLOCK_SKEW || tsA1 > ts + NTCP_CLOCK_SKEW)
		{
			LogPrint (eLogError, "NTCP: Phase3 time difference ", ts - tsA1, " exceeds clock skew"); 
			Terminate ();
			return;
		}	

		// check signature
		SignedData s;
		s.Insert (m_Establisher->phase1.pubKey, 256); // x
		s.Insert (m_Establisher->phase2.pubKey, 256); // y
		s.Insert (i2p::context.GetRouterInfo ().GetIdentHash (), 32); // ident
		s.Insert (tsA); // tsA
		s.Insert (tsB); // tsB			
		if (!s.Verify (m_RemoteIdentity, buf))
		{	
			LogPrint (eLogError, "NTCP: signature verification failed");
			Terminate ();
			return;
		}	

		SendPhase4 (tsA, tsB);
	}

	void NTCPSession::SendPhase4 (uint32_t tsA, uint32_t tsB)
	{
		SignedData s;
		s.Insert (m_Establisher->phase1.pubKey, 256); // x
		s.Insert (m_Establisher->phase2.pubKey, 256); // y
		s.Insert (m_RemoteIdentity->GetIdentHash (), 32); // ident
		s.Insert (tsA); // tsA
		s.Insert (tsB); // tsB
		auto keys = i2p::context.GetPrivateKeys ();
 		auto signatureLen = keys.GetPublic ()->GetSignatureLen ();
		s.Sign (keys, m_ReceiveBuffer);
		size_t paddingSize = signatureLen & 0x0F; // %16
		if (paddingSize > 0) signatureLen += (16 - paddingSize);		
		m_Encryption.Encrypt (m_ReceiveBuffer, signatureLen, m_ReceiveBuffer);

		boost::asio::async_write (m_Socket, boost::asio::buffer (m_ReceiveBuffer, signatureLen), boost::asio::transfer_all (),
        	std::bind(&NTCPSession::HandlePhase4Sent, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
	}	

	void NTCPSession::HandlePhase4Sent (const boost::system::error_code& ecode,  std::size_t bytes_transferred)
	{
		(void) bytes_transferred;
		if (ecode)
        {
			LogPrint (eLogWarning, "NTCP: Couldn't send Phase 4 message: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
		{	
			LogPrint (eLogInfo, "NTCP: Server session from ", m_Socket.remote_endpoint (), " connected");
			m_Server.AddNTCPSession (shared_from_this ());

			Connected ();
			m_ReceiveBufferOffset = 0;
			m_NextMessage = nullptr;
			Receive ();
		}	
	}	
		
	void NTCPSession::HandlePhase4Received (const boost::system::error_code& ecode, std::size_t bytes_transferred, uint32_t tsA)
	{
		if (ecode)
        {
			LogPrint (eLogError, "NTCP: Phase 4 read error: ", ecode.message (), ". Check your clock");
			if (ecode != boost::asio::error::operation_aborted)
			{
				 // this router doesn't like us	
				i2p::data::netdb.SetUnreachable (GetRemoteIdentity ()->GetIdentHash (), true);
				Terminate ();
			}	
		}
		else
		{	
			m_Decryption.Decrypt(m_ReceiveBuffer, bytes_transferred, m_ReceiveBuffer);

			// check timestamp
			uint32_t tsB = bufbe32toh (m_Establisher->phase2.encrypted.timestamp);
			auto ts = i2p::util::GetSecondsSinceEpoch ();
			if (tsB < ts - NTCP_CLOCK_SKEW || tsB > ts + NTCP_CLOCK_SKEW)
			{
				LogPrint (eLogError, "NTCP: Phase4 time difference ", ts - tsB, " exceeds clock skew"); 
				Terminate ();
				return;
			}	
			
			// verify signature
			SignedData s;
			s.Insert (m_Establisher->phase1.pubKey, 256); // x
			s.Insert (m_Establisher->phase2.pubKey, 256); // y
			s.Insert (i2p::context.GetIdentHash (), 32); // ident
			s.Insert (tsA); // tsA
			s.Insert (m_Establisher->phase2.encrypted.timestamp, 4); // tsB

			if (!s.Verify (m_RemoteIdentity, m_ReceiveBuffer))
			{	
				LogPrint (eLogError, "NTCP: Phase 4 process error: signature verification failed");
				Terminate ();
				return;
			}	
			LogPrint (eLogDebug, "NTCP: session to ", m_Socket.remote_endpoint (), " connected");
			Connected ();
						
			m_ReceiveBufferOffset = 0;
			m_NextMessage = nullptr;
			Receive ();
		}
	}

	void NTCPSession::Receive ()
	{
		m_Socket.async_read_some (boost::asio::buffer(m_ReceiveBuffer + m_ReceiveBufferOffset, NTCP_BUFFER_SIZE - m_ReceiveBufferOffset),                
			std::bind(&NTCPSession::HandleReceived, shared_from_this (), 
			std::placeholders::_1, std::placeholders::_2));
	}	
		
	void NTCPSession::HandleReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred)
	{
		if (ecode) {
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogDebug, "NTCP: Read error: ", ecode.message ());
			if (!m_NumReceivedBytes)
				m_Server.Ban (m_ConnectedFrom);
			//if (ecode != boost::asio::error::operation_aborted)
			Terminate ();
		}
		else
		{
			m_NumReceivedBytes += bytes_transferred;
			i2p::transport::transports.UpdateReceivedBytes (bytes_transferred);
			m_ReceiveBufferOffset += bytes_transferred;

			if (m_ReceiveBufferOffset >= 16)
			{	
				int numReloads = 0;
				do
				{	
					uint8_t * nextBlock = m_ReceiveBuffer;
					while (m_ReceiveBufferOffset >= 16)
					{
						if (!DecryptNextBlock (nextBlock)) // 16 bytes
						{
							Terminate ();
							return; 
						}	
						nextBlock += 16;
						m_ReceiveBufferOffset -= 16;
					}	
					if (m_ReceiveBufferOffset > 0)
						memcpy (m_ReceiveBuffer, nextBlock, m_ReceiveBufferOffset);

					// try to read more
					if (numReloads < 5)
					{	
						boost::system::error_code ec;
						size_t moreBytes = m_Socket.available(ec);
						if (moreBytes)
						{
							if (moreBytes > NTCP_BUFFER_SIZE - m_ReceiveBufferOffset)
								moreBytes = NTCP_BUFFER_SIZE - m_ReceiveBufferOffset;
							moreBytes = m_Socket.read_some (boost::asio::buffer (m_ReceiveBuffer + m_ReceiveBufferOffset, moreBytes));
							if (ec)
							{
								LogPrint (eLogInfo, "NTCP: Read more bytes error: ", ec.message ());
								Terminate ();
								return;
							}	
							m_NumReceivedBytes += moreBytes;
							m_ReceiveBufferOffset += moreBytes;
							numReloads++;
						}	
					}	
				}	
				while (m_ReceiveBufferOffset >= 16);
				m_Handler.Flush ();
			}	
			
			ScheduleTermination (); // reset termination timer
			Receive ();
		}	
	}	

	bool NTCPSession::DecryptNextBlock (const uint8_t * encrypted) // 16 bytes
	{
		if (!m_NextMessage) // new message, header expected
		{	
			// decrypt header and extract length
			uint8_t buf[16];
			m_Decryption.Decrypt (encrypted, buf);
			uint16_t dataSize = bufbe16toh (buf);
			if (dataSize)
			{
				// new message
				if (dataSize + 16U > NTCP_MAX_MESSAGE_SIZE - 2) // + 6 + padding
				{
					LogPrint (eLogError, "NTCP: data size ", dataSize, " exceeds max size");
					return false;
				}
				auto msg = (dataSize + 16U) <= I2NP_MAX_SHORT_MESSAGE_SIZE - 2 ? NewI2NPShortMessage () : NewI2NPMessage ();
				m_NextMessage = msg;	
				memcpy (m_NextMessage->buf, buf, 16);
				m_NextMessageOffset = 16;
				m_NextMessage->offset = 2; // size field
				m_NextMessage->len = dataSize + 2; 
			}	
			else
			{	
				// timestamp
				LogPrint (eLogDebug, "NTCP: Timestamp");
				return true;
			}	
		}	
		else // message continues
		{	
			m_Decryption.Decrypt (encrypted, m_NextMessage->buf + m_NextMessageOffset);
			m_NextMessageOffset += 16;
		}		
		
		if (m_NextMessageOffset >= m_NextMessage->len + 4) // +checksum
		{	
			// we have a complete I2NP message
			uint8_t checksum[4];
			htobe32buf (checksum, adler32 (adler32 (0, Z_NULL, 0), m_NextMessage->buf, m_NextMessageOffset - 4));
			if (!memcmp (m_NextMessage->buf + m_NextMessageOffset - 4, checksum, 4))
			{
				if (!m_NextMessage->IsExpired ())
					m_Handler.PutNextMessage (m_NextMessage);
				else
					LogPrint (eLogInfo, "NTCP: message expired");
			}	
			else
				LogPrint (eLogWarning, "NTCP: Incorrect adler checksum of message, dropped");
			m_NextMessage = nullptr;
		}
		return true;	
 	}	

	void NTCPSession::Send (std::shared_ptr<i2p::I2NPMessage> msg)
	{
		m_IsSending = true;
		boost::asio::async_write (m_Socket, CreateMsgBuffer (msg), boost::asio::transfer_all (),                      
        	std::bind(&NTCPSession::HandleSent, shared_from_this (), std::placeholders::_1, std::placeholders::_2, std::vector<std::shared_ptr<I2NPMessage> >{ msg }));	
	}

	boost::asio::const_buffers_1 NTCPSession::CreateMsgBuffer (std::shared_ptr<I2NPMessage> msg)
	{
		uint8_t * sendBuffer;
		int len;

		if (msg)
		{	
			// regular I2NP
			if (msg->offset < 2)
				LogPrint (eLogError, "NTCP: Malformed I2NP message"); // TODO:
			sendBuffer = msg->GetBuffer () - 2; 
			len = msg->GetLength ();
			htobe16buf (sendBuffer, len);
		}	
		else
		{
			// prepare timestamp
			sendBuffer = m_TimeSyncBuffer;
			len = 4;
			htobuf16(sendBuffer, 0);
			htobe32buf (sendBuffer + 2, time (0));
		}	
		int rem = (len + 6) & 0x0F; // %16
		int padding = 0;
		if (rem > 0) {
			padding = 16 - rem;
			// fill with random padding
			RAND_bytes(sendBuffer + len + 2, padding);
		}
		htobe32buf (sendBuffer + len + 2 + padding, adler32 (adler32 (0, Z_NULL, 0), sendBuffer, len + 2+ padding));

		int l = len + padding + 6;
		m_Encryption.Encrypt(sendBuffer, l, sendBuffer);	
		return boost::asio::buffer ((const uint8_t *)sendBuffer, l);
	}	


	void NTCPSession::Send (const std::vector<std::shared_ptr<I2NPMessage> >& msgs)
	{
		m_IsSending = true;
		std::vector<boost::asio::const_buffer> bufs;
		for (auto it: msgs)
			bufs.push_back (CreateMsgBuffer (it));
		boost::asio::async_write (m_Socket, bufs, boost::asio::transfer_all (),                      
        	std::bind(&NTCPSession::HandleSent, shared_from_this (), std::placeholders::_1, std::placeholders::_2, msgs));
	}
		
	void NTCPSession::HandleSent (const boost::system::error_code& ecode, std::size_t bytes_transferred, std::vector<std::shared_ptr<I2NPMessage> > msgs)
	{
		(void) msgs;
		m_IsSending = false;
		if (ecode)
        {
			LogPrint (eLogWarning, "NTCP: Couldn't send msgs: ", ecode.message ());
			// we shouldn't call Terminate () here, because HandleReceive takes care
			// TODO: 'delete this' statement in Terminate () must be eliminated later
			// Terminate ();
		}
		else
		{	
			m_NumSentBytes += bytes_transferred;
			i2p::transport::transports.UpdateSentBytes (bytes_transferred);
			if (!m_SendQueue.empty())
			{
				Send (m_SendQueue);
				m_SendQueue.clear ();
			}	
			else
				ScheduleTermination (); // reset termination timer
		}	
	}	

		
	void NTCPSession::SendTimeSyncMessage ()
	{
		Send (nullptr);
	}	


	void NTCPSession::SendI2NPMessages (const std::vector<std::shared_ptr<I2NPMessage> >& msgs)
	{
		m_Server.GetService ().post (std::bind (&NTCPSession::PostI2NPMessages, shared_from_this (), msgs));  
	}	

	void NTCPSession::PostI2NPMessages (std::vector<std::shared_ptr<I2NPMessage> > msgs)
	{
		if (m_IsTerminated) return;
		if (m_IsSending)
		{
			for (auto it: msgs)
				m_SendQueue.push_back (it);
		}	
		else	
			Send (msgs);
	}	
		
	void NTCPSession::ScheduleTermination ()
	{
		m_TerminationTimer.cancel ();
		m_TerminationTimer.expires_from_now (boost::posix_time::seconds(NTCP_TERMINATION_TIMEOUT));
		m_TerminationTimer.async_wait (std::bind (&NTCPSession::HandleTerminationTimer,
			shared_from_this (), std::placeholders::_1));
	}

	void NTCPSession::HandleTerminationTimer (const boost::system::error_code& ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{	
			LogPrint (eLogDebug, "NTCP: No activity for ", NTCP_TERMINATION_TIMEOUT, " seconds");
			//Terminate ();
			m_Socket.close ();// invoke Terminate () from HandleReceive 
		}	
	}	

//-----------------------------------------
	NTCPServer::NTCPServer ():
		m_IsRunning (false), m_Thread (nullptr), m_Work (m_Service), 
		m_NTCPAcceptor (nullptr), m_NTCPV6Acceptor (nullptr)
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
			// create acceptors
			auto addresses = context.GetRouterInfo ().GetAddresses ();
			for (auto& address : addresses)
			{
				if (address.transportStyle == i2p::data::RouterInfo::eTransportNTCP && address.host.is_v4 ())
				{	
					m_NTCPAcceptor = new boost::asio::ip::tcp::acceptor (m_Service,
						boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), address.port));

					LogPrint (eLogInfo, "NTCP: Start listening TCP port ", address.port);
					auto conn = std::make_shared<NTCPSession>(*this);
					m_NTCPAcceptor->async_accept(conn->GetSocket (), std::bind (&NTCPServer::HandleAccept, this, 
						conn, std::placeholders::_1));	
				
					if (context.SupportsV6 ())
					{
						m_NTCPV6Acceptor = new boost::asio::ip::tcp::acceptor (m_Service);
						m_NTCPV6Acceptor->open (boost::asio::ip::tcp::v6());
						m_NTCPV6Acceptor->set_option (boost::asio::ip::v6_only (true));
						m_NTCPV6Acceptor->bind (boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v6(), address.port));
						m_NTCPV6Acceptor->listen ();

						LogPrint (eLogInfo, "NTCP: Start listening V6 TCP port ", address.port);
						auto conn = std::make_shared<NTCPSession> (*this);
						m_NTCPV6Acceptor->async_accept(conn->GetSocket (), std::bind (&NTCPServer::HandleAcceptV6,
							this, conn, std::placeholders::_1));
					}	
				}	
			}	
		}	
	}
		
	void NTCPServer::Stop ()
	{	
		m_NTCPSessions.clear ();

		if (m_IsRunning)
		{	
			m_IsRunning = false;
			delete m_NTCPAcceptor;
			m_NTCPAcceptor = nullptr;
			delete m_NTCPV6Acceptor;
			m_NTCPV6Acceptor = nullptr;

			m_Service.stop ();
			if (m_Thread)
			{	
				m_Thread->join (); 
				delete m_Thread;
				m_Thread = nullptr;
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
				auto it = m_BanList.find (ep.address ());
				if (it != m_BanList.end ())
				{
					uint32_t ts = i2p::util::GetSecondsSinceEpoch ();
					if (ts < it->second)
					{
						LogPrint (eLogWarning, "NTCP: ", ep.address (), " is banned for ", it->second - ts, " more seconds");
						conn = nullptr;
					}
					else
						m_BanList.erase (it);
				}
				if (conn)
					conn->ServerLogin ();
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
				auto it = m_BanList.find (ep.address ());
				if (it != m_BanList.end ())
				{
					uint32_t ts = i2p::util::GetSecondsSinceEpoch ();
					if (ts < it->second)
					{
						LogPrint (eLogWarning, "NTCP: ", ep.address (), " is banned for ", it->second - ts, " more seconds");
						conn = nullptr;
					}
					else
						m_BanList.erase (it);
				}
				if (conn)
					conn->ServerLogin ();
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

	void NTCPServer::Connect (const boost::asio::ip::address& address, int port, std::shared_ptr<NTCPSession> conn)
	{
		LogPrint (eLogDebug, "NTCP: Connecting to ", address ,":",  port);
		m_Service.post([=]()
		{           
			if (this->AddNTCPSession (conn))
				conn->GetSocket ().async_connect (boost::asio::ip::tcp::endpoint (address, port), 
					std::bind (&NTCPServer::HandleConnect, this, std::placeholders::_1, conn));	
		});	
	}

	void NTCPServer::HandleConnect (const boost::system::error_code& ecode, std::shared_ptr<NTCPSession> conn)
	{
		if (ecode)
        {
			LogPrint (eLogError, "NTCP: Connect error: ", ecode.message ());
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

	void NTCPServer::Ban (const boost::asio::ip::address& addr)
	{
		uint32_t ts = i2p::util::GetSecondsSinceEpoch ();	
		m_BanList[addr] = ts + NTCP_BAN_EXPIRATION_TIMEOUT;
		LogPrint (eLogWarning, "NTCP: ", addr, " has been banned for ", NTCP_BAN_EXPIRATION_TIMEOUT, " seconds");
	}
}	
}	
