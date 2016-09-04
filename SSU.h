#ifndef SSU_H__
#define SSU_H__

#include <inttypes.h>
#include <string.h>
#include <chrono>
#include <map>
#include <list>
#include <set>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include "Crypto.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "RouterInfo.h"
#include "I2NPProtocol.h"
#include "SSUSession.h"

namespace i2p
{
namespace transport
{
  const size_t SSU_MAX_NUM_INTRODUCERS = 3;
  const std::chrono::seconds SSU_KEEP_ALIVE_INTERVAL(30);
  const std::chrono::minutes SSU_PEER_TEST_TIMEOUT(1);
  const std::chrono::hours SSU_TO_INTRODUCER_SESSION_DURATION(1);
  const std::chrono::seconds SSU_TERMINATION_CHECK_TIMEOUT(30);

	struct SSUPacket
	{
		i2p::crypto::AESAlignedBuffer<1500> buf;
		boost::asio::ip::udp::endpoint from;
		size_t len;
	};	
	
	class SSUServer
	{
		public:

    typedef std::chrono::milliseconds TimeDuration;
    typedef boost::asio::deadline_timer Timer;

    /** expire a timer using our time types */
    template<typename duration>
    static void ExpireTimer(Timer & timer, duration d)
    {
      boost::posix_time::milliseconds expire(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
      timer.expires_from_now(expire);
    }
    
			SSUServer (int port);
			SSUServer (const boost::asio::ip::address & addr, int port);			// ipv6 only constructor
			~SSUServer ();
			void Start ();
			void Stop ();
			void CreateSession (std::shared_ptr<const i2p::data::RouterInfo> router, bool peerTest = false);
			void CreateSession (std::shared_ptr<const i2p::data::RouterInfo> router, 
				const boost::asio::ip::address& addr, int port, bool peerTest = false);
			void CreateDirectSession (std::shared_ptr<const i2p::data::RouterInfo> router, boost::asio::ip::udp::endpoint remoteEndpoint, bool peerTest);
			std::shared_ptr<SSUSession> FindSession (std::shared_ptr<const i2p::data::RouterInfo> router) const;
			std::shared_ptr<SSUSession> FindSession (const boost::asio::ip::udp::endpoint& e) const;
			std::shared_ptr<SSUSession> GetRandomEstablishedV4Session (std::shared_ptr<const SSUSession> excluded);
			std::shared_ptr<SSUSession> GetRandomEstablishedV6Session (std::shared_ptr<const SSUSession> excluded);
			void DeleteSession (std::shared_ptr<SSUSession> session);
			void DeleteAllSessions ();			

			boost::asio::io_service& GetService () { return m_Service; };
			boost::asio::io_service& GetServiceV6 () { return m_ServiceV6; };
			const boost::asio::ip::udp::endpoint& GetEndpoint () const { return m_Endpoint; };			
			void Send (const uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint& to);
			void AddRelay (uint32_t tag, const boost::asio::ip::udp::endpoint& relay);
			std::shared_ptr<SSUSession> FindRelaySession (uint32_t tag);

			void NewPeerTest (uint32_t nonce, PeerTestParticipant role, std::shared_ptr<SSUSession> session = nullptr);
			PeerTestParticipant GetPeerTestParticipant (uint32_t nonce);
			std::shared_ptr<SSUSession> GetPeerTestSession (uint32_t nonce);
			void UpdatePeerTest (uint32_t nonce, PeerTestParticipant role);
			void RemovePeerTest (uint32_t nonce);
      
    typedef std::map<boost::asio::ip::udp::endpoint, std::shared_ptr<SSUSession> > SessionMap;

  private:

    
			void Run ();
			void RunV6 ();
			void RunReceivers ();
			void Receive ();
			void ReceiveV6 ();
			void HandleReceivedFrom (const boost::system::error_code& ecode, std::size_t bytes_transferred, SSUPacket * packet);
			void HandleReceivedFromV6 (const boost::system::error_code& ecode, std::size_t bytes_transferred, SSUPacket * packet);
			void HandleReceivedPackets (std::vector<SSUPacket *> packets, SessionMap* sessions);

			void CreateSessionThroughIntroducer (std::shared_ptr<const i2p::data::RouterInfo> router, bool peerTest = false);			
			template<typename Filter>
			std::shared_ptr<SSUSession> GetRandomV4Session (Filter filter);
			template<typename Filter>
			std::shared_ptr<SSUSession> GetRandomV6Session (Filter filter);			

			std::set<SSUSession *> FindIntroducers (int maxNumIntroducers);	
			void ScheduleIntroducersUpdateTimer ();
			void HandleIntroducersUpdateTimer (const boost::system::error_code& ecode);

			void SchedulePeerTestsCleanupTimer ();
			void HandlePeerTestsCleanupTimer (const boost::system::error_code& ecode);

			// timer
			void ScheduleTermination ();
			void HandleTerminationTimer (const boost::system::error_code& ecode);
			void ScheduleTerminationV6 ();
			void HandleTerminationTimerV6 (const boost::system::error_code& ecode);
    

    void ScheduleSessionTick(const bool isv6);
    void Tick(const boost::system::error_code& ecode, SessionMap & sessions, const bool isv6);

		private:

			struct PeerTest
			{
        std::chrono::milliseconds creationTime;
				PeerTestParticipant role;
				std::shared_ptr<SSUSession> session; // for Bob to Alice
			};
			
			bool m_OnlyV6;			
			bool m_IsRunning;
			std::thread * m_Thread, * m_ThreadV6, * m_ReceiversThread;	
			boost::asio::io_service m_Service, m_ServiceV6, m_ReceiversService;
			boost::asio::io_service::work m_Work, m_WorkV6, m_ReceiversWork;
			boost::asio::ip::udp::endpoint m_Endpoint, m_EndpointV6;
			boost::asio::ip::udp::socket m_Socket, m_SocketV6;
			Timer m_IntroducersUpdateTimer, m_PeerTestsCleanupTimer,
				m_TerminationTimer, m_TerminationTimerV6, m_SessionTickerTimer, m_SessionTickerTimerV6;
			std::list<boost::asio::ip::udp::endpoint> m_Introducers; // introducers we are connected to   
			SessionMap m_Sessions, m_SessionsV6;
			std::map<uint32_t, boost::asio::ip::udp::endpoint> m_Relays; // we are introducer
			std::map<uint32_t, PeerTest> m_PeerTests; // nonce -> creation time in milliseconds

		public:
			// for HTTP only
			const decltype(m_Sessions)& GetSessions () const { return m_Sessions; };
			const decltype(m_SessionsV6)& GetSessionsV6 () const { return m_SessionsV6; };
	};
}
}

#endif

