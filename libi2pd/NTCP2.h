/*
* Copyright (c) 2013-2018, The PurpleI2P Project
*
* This file is part of Purple i2pd project and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*
* Kovri go write your own code
*
*/
#ifndef NTCP2_H__
#define NTCP2_H__

#include <inttypes.h>
#include <memory>
#include <thread>
#include <list>
#include <map>
#include <array>
#include <openssl/bn.h>
#include <boost/asio.hpp>
#include "util.h"
#include "RouterInfo.h"
#include "TransportSession.h"

namespace i2p
{
namespace transport
{

	const size_t NTCP2_UNENCRYPTED_FRAME_MAX_SIZE = 65519;	
	const int NTCP2_MAX_PADDING_RATIO = 6; // in %

	const int NTCP2_CONNECT_TIMEOUT = 5; // 5 seconds
	const int NTCP2_ESTABLISH_TIMEOUT = 10; // 10 seconds
	const int NTCP2_TERMINATION_TIMEOUT = 120; // 2 minutes
	const int NTCP2_TERMINATION_CHECK_TIMEOUT = 30; // 30 seconds

	const int NTCP2_CLOCK_SKEW = 60; // in seconds	

	enum NTCP2BlockType
	{
		eNTCP2BlkDateTime = 0,
		eNTCP2BlkOptions, // 1
		eNTCP2BlkRouterInfo, // 2
		eNTCP2BlkI2NPMessage, // 3
		eNTCP2BlkTermination, // 4
		eNTCP2BlkPadding = 254	
	};	

	enum NTCP2TerminationReason
	{
		eNTCP2NormalClose = 0,
		eNTCP2TerminationReceived, // 1
		eNTCP2IdleTimeout, // 2
		eNTCP2RouterShutdown, // 3
		eNTCP2DataPhaseAEADFailure, // 4
		eNTCP2IncompatibleOptions, // 5
		eNTCP2IncompatibleSignatureType, // 6
		eNTCP2ClockSkew, // 7
		eNTCP2PaddingViolation, // 8
		eNTCP2AEADFramingError, // 9
		eNTCP2PayloadFormatError, // 10
		eNTCP2Message1Error, // 11
		eNTCP2Message2Error, // 12
		eNTCP2Message3Error, // 13
		eNTCP2IntraFrameReadTimeout, // 14
		eNTCP2RouterInfoSignatureVerificationFail, // 15
		eNTCP2IncorrectSParameter, // 16
		eNTCP2Banned, // 17
	};		
	

	typedef std::array<uint8_t, NTCP2_UNENCRYPTED_FRAME_MAX_SIZE> NTCP2FrameBuffer;
	struct NTCP2Establisher
	{
		NTCP2Establisher ();
		~NTCP2Establisher ();
		
		const uint8_t * GetPub () const { return m_EphemeralPublicKey; };
		const uint8_t * GetPriv () const { return m_EphemeralPrivateKey; };
		const uint8_t * GetRemotePub () const { return m_RemoteEphemeralPublicKey; }; // Y for Alice and X for Bob
		uint8_t * GetRemotePub () { return m_RemoteEphemeralPublicKey; }; // to set

		const uint8_t * GetK () const { return m_K; };
		const uint8_t * GetCK () const { return m_CK; };
		const uint8_t * GetH () const { return m_H; };

		void KDF1Alice ();
		void KDF1Bob ();
		void KDF2Alice ();
		void KDF2Bob ();
		void KDF3Alice (); // for SessionConfirmed part 2
		void KDF3Bob ();

		void MixKey (const uint8_t * inputKeyMaterial, uint8_t * derived);
		void KeyDerivationFunction1 (const uint8_t * pub, const uint8_t * priv, const uint8_t * rs, const uint8_t * epub); // for SessionRequest, (pub, priv) for DH
		void KeyDerivationFunction2 (const uint8_t * sessionRequest, size_t sessionRequestLen, const uint8_t * epub); // for SessionCreate
		void CreateEphemeralKey ();

		void CreateSessionRequestMessage ();
		void CreateSessionCreatedMessage ();
		void CreateSessionConfirmedMessagePart1 (const uint8_t * nonce);
		void CreateSessionConfirmedMessagePart2 (const uint8_t * nonce);

		bool ProcessSessionRequestMessage (uint16_t& paddingLen);
		bool ProcessSessionCreatedMessage (uint16_t& paddingLen);
		bool ProcessSessionConfirmedMessagePart1 (const uint8_t * nonce);
		bool ProcessSessionConfirmedMessagePart2 (const uint8_t * nonce, uint8_t * m3p2Buf);

		BN_CTX * m_Ctx;
		uint8_t m_EphemeralPrivateKey[32], m_EphemeralPublicKey[32], m_RemoteEphemeralPublicKey[32]; // x25519
		uint8_t m_RemoteStaticKey[32], m_IV[16], m_H[32] /*h*/, m_CK[33] /*ck*/, m_K[32] /*k*/;
		i2p::data::IdentHash m_RemoteIdentHash;
		uint16_t m3p2Len; 

		uint8_t * m_SessionRequestBuffer, * m_SessionCreatedBuffer, * m_SessionConfirmedBuffer;
		size_t m_SessionRequestBufferLen, m_SessionCreatedBufferLen;

	};		

	class NTCP2Server;
	class NTCP2Session: public TransportSession, public std::enable_shared_from_this<NTCP2Session>
	{
		public:

			NTCP2Session (NTCP2Server& server, std::shared_ptr<const i2p::data::RouterInfo> in_RemoteRouter = nullptr); 
			~NTCP2Session ();
			void Terminate ();
			void TerminateByTimeout ();
			void Done ();

			boost::asio::ip::tcp::socket& GetSocket () { return m_Socket; };

			bool IsEstablished () const { return m_IsEstablished; };
			bool IsTerminated () const { return m_IsTerminated; };

			void ClientLogin (); // Alice 
			void ServerLogin (); // Bob
		
			void SendLocalRouterInfo (); // after handshake
			void SendI2NPMessages (const std::vector<std::shared_ptr<I2NPMessage> >& msgs);

		private:

			void Established ();

			void CreateNonce (uint64_t seqn, uint8_t * nonce);
			void KeyDerivationFunctionDataPhase ();

			// establish
			void SendSessionRequest ();
			void SendSessionCreated ();
			void SendSessionConfirmed ();

			void HandleSessionRequestSent (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandleSessionRequestReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandleSessionRequestPaddingReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandleSessionCreatedSent (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandleSessionCreatedReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandleSessionCreatedPaddingReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandleSessionConfirmedSent (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void HandleSessionConfirmedReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred);

			// data
			void ReceiveLength ();
			void HandleReceivedLength (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void Receive ();
			void HandleReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void ProcessNextFrame (const uint8_t * frame, size_t len);

			void SendNextFrame (const uint8_t * payload, size_t len); 
			void HandleNextFrameSent (const boost::system::error_code& ecode, std::size_t bytes_transferred);
			void SendQueue ();
			void SendRouterInfo ();
			void SendTermination (NTCP2TerminationReason reason);
			void SendTerminationAndTerminate (NTCP2TerminationReason reason);
			void PostI2NPMessages (std::vector<std::shared_ptr<I2NPMessage> > msgs);

		private:

			NTCP2Server& m_Server;
			boost::asio::ip::tcp::socket m_Socket;
			bool m_IsEstablished, m_IsTerminated;

			std::unique_ptr<NTCP2Establisher> m_Establisher;
			// data phase
			uint8_t m_Kab[33], m_Kba[32], m_Sipkeysab[33], m_Sipkeysba[32]; 
			const uint8_t * m_SendKey, * m_ReceiveKey, * m_SendSipKey, * m_ReceiveSipKey;
			uint16_t m_NextReceivedLen; 
			uint8_t * m_NextReceivedBuffer, * m_NextSendBuffer;
			union
			{
				uint8_t buf[8];
				uint16_t key;
			} m_ReceiveIV, m_SendIV;
			uint64_t m_ReceiveSequenceNumber, m_SendSequenceNumber;

			i2p::I2NPMessagesHandler m_Handler;

			bool m_IsSending;
			std::list<std::shared_ptr<I2NPMessage> > m_SendQueue;
	};

	class NTCP2Server
	{
		public:

			NTCP2Server ();
			~NTCP2Server ();

			void Start ();
			void Stop ();

			bool AddNTCP2Session (std::shared_ptr<NTCP2Session> session);
			void RemoveNTCP2Session (std::shared_ptr<NTCP2Session> session);
			std::shared_ptr<NTCP2Session> FindNTCP2Session (const i2p::data::IdentHash& ident);

			boost::asio::io_service& GetService () { return m_Service; };
		
			void Connect(const boost::asio::ip::address & address, uint16_t port, std::shared_ptr<NTCP2Session> conn);

			NTCP2FrameBuffer * NewNTCP2FrameBuffer () { return m_NTCP2FrameBuffersPool.Acquire(); }
			void DeleteNTCP2FrameBuffer (NTCP2FrameBuffer * buf) { return m_NTCP2FrameBuffersPool.Release(buf); }

		private:

			void Run ();
			void HandleAccept (std::shared_ptr<NTCP2Session> conn, const boost::system::error_code& error);
			void HandleAcceptV6 (std::shared_ptr<NTCP2Session> conn, const boost::system::error_code& error);

			void HandleConnect (const boost::system::error_code& ecode, std::shared_ptr<NTCP2Session> conn, std::shared_ptr<boost::asio::deadline_timer> timer);		

			// timer
			void ScheduleTermination ();
			void HandleTerminationTimer (const boost::system::error_code& ecode);

		private:

			bool m_IsRunning;
			std::thread * m_Thread;
			boost::asio::io_service m_Service;
			boost::asio::io_service::work m_Work;
			boost::asio::deadline_timer m_TerminationTimer;
			std::unique_ptr<boost::asio::ip::tcp::acceptor> m_NTCP2Acceptor, m_NTCP2V6Acceptor;
			std::map<i2p::data::IdentHash, std::shared_ptr<NTCP2Session> > m_NTCP2Sessions; 
			std::list<std::shared_ptr<NTCP2Session> > m_PendingIncomingSessions;

			i2p::util::MemoryPool<NTCP2FrameBuffer> m_NTCP2FrameBuffersPool;

		public:

			// for HTTP/I2PControl
			const decltype(m_NTCP2Sessions)& GetNTCP2Sessions () const { return m_NTCP2Sessions; };
	};
}
}

#endif
