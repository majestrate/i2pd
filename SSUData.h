#ifndef SSU_DATA_H__
#define SSU_DATA_H__

#include <inttypes.h>
#include <string.h>
#include <map>
#include <vector>
#include <unordered_set>
#include <memory>
#include <boost/asio.hpp>
#include "I2NPProtocol.h"
#include "Identity.h"
#include "RouterInfo.h"
#include "Log.h"
namespace i2p
{
namespace transport
{

	const size_t SSU_MTU_V4 = 1484;
	#ifdef MESHNET
	const size_t SSU_MTU_V6 = 1286;
	#else
	const size_t SSU_MTU_V6 = 1472;
	#endif
	const size_t IPV4_HEADER_SIZE = 20;
	const size_t IPV6_HEADER_SIZE = 40;	
	const size_t UDP_HEADER_SIZE = 8;
	const size_t SSU_V4_MAX_PACKET_SIZE = SSU_MTU_V4 - IPV4_HEADER_SIZE - UDP_HEADER_SIZE; // 1456
	const size_t SSU_V6_MAX_PACKET_SIZE = SSU_MTU_V6 - IPV6_HEADER_SIZE - UDP_HEADER_SIZE; // 1424
	const std::chrono::seconds RESEND_INTERVAL(3);
	const size_t MAX_NUM_RESENDS = 5;
  const std::chrono::seconds DECAY_INTERVAL(20);
	const std::chrono::seconds INCOMPLETE_MESSAGES_CLEANUP_TIMEOUT(30);
	const size_t MAX_NUM_RECEIVED_MESSAGES = 1000; // how many msgID we store for duplicates check
	const size_t MAX_OUTGOING_WINDOW_SIZE = 200; // how many unacked message we can store
	// data flags
	const uint8_t DATA_FLAG_EXTENDED_DATA_INCLUDED = 0x02;
	const uint8_t DATA_FLAG_WANT_REPLY = 0x04;
	const uint8_t DATA_FLAG_REQUEST_PREVIOUS_ACKS = 0x08;
	const uint8_t DATA_FLAG_EXPLICIT_CONGESTION_NOTIFICATION = 0x10;
	const uint8_t DATA_FLAG_ACK_BITFIELDS_INCLUDED = 0x40;
	const uint8_t DATA_FLAG_EXPLICIT_ACKS_INCLUDED = 0x80;	

	struct Fragment
	{
		int fragmentNum;
		size_t len;
		bool isLast;
		uint8_t buf[SSU_V4_MAX_PACKET_SIZE + 18]; // use biggest

		Fragment () = default;
		Fragment (int n, const uint8_t * b, int l, bool last): 
			fragmentNum (n), len (l), isLast (last) { memcpy (buf, b, len); };		
	};	

	struct FragmentCmp
	{
		bool operator() (const std::unique_ptr<Fragment>& f1, const std::unique_ptr<Fragment>& f2) const
  		{	
			return f1->fragmentNum < f2->fragmentNum; 
		};
	};	

  template<typename Time>
	struct IncompleteMessage
	{
		std::shared_ptr<I2NPMessage> msg;
		int nextFragmentNum;
    
    Time lastFragmentInsertTime; // since epoch
		std::set<std::unique_ptr<Fragment>, FragmentCmp> savedFragments;
		
		IncompleteMessage (std::shared_ptr<I2NPMessage> m): msg (m), nextFragmentNum (0), lastFragmentInsertTime (0) {};
		void AttachNextFragment (const uint8_t * fragment, size_t fragmentSize)
    {
      if (msg->len + fragmentSize > msg->maxLen)
      {
        LogPrint (eLogWarning, "SSU: I2NP message size ", msg->maxLen, " is not enough");
        auto newMsg = NewI2NPMessage ();
        *newMsg = *msg;
        msg = newMsg;
      }
      if (msg->Concat (fragment, fragmentSize) < fragmentSize)
        LogPrint (eLogError, "SSU: I2NP buffer overflow ", msg->maxLen);
      nextFragmentNum++;
    }
	};

  template<typename Time>
	struct SentMessage
	{
		std::vector<std::unique_ptr<Fragment> > fragments;
		Time nextResendTime; // since epoch
		size_t numResends;
	};	
	
	class SSUSession;
	class SSUData
	{
		public:

    typedef std::chrono::milliseconds Time;
    typedef SentMessage<Time> OutboundMessage;
    typedef IncompleteMessage<Time> InboundMessage;
    
			SSUData (SSUSession& session); 
			~SSUData ();

			void Start ();
			void Stop ();	
			
			void ProcessMessage (uint8_t * buf, size_t len);
			void FlushReceivedMessage ();
			void Send (std::shared_ptr<i2p::I2NPMessage> msg);

			void AdjustPacketSize (std::shared_ptr<const i2p::data::RouterInfo> remoteRouter);	
			void UpdatePacketSize (const i2p::data::IdentHash& remoteIdent);

    /** 
     *  called every interval to do outbound message queue resend and clean up inbound message queue, 
     *  returns true if we should tick again, returns false if this session should be closed 
     */
    bool Tick(const Time now);
		private:

			void SendMsgAck (uint32_t msgID);
			void SendFragmentAck (uint32_t msgID, int fragmentNum);
			void ProcessAcks (uint8_t *& buf, uint8_t flag);
			void ProcessFragments (uint8_t * buf);
			void ProcessSentMessageAck (uint32_t msgID);	

    /*
			void ScheduleResend ();
    
			void HandleResendTimer (const boost::system::error_code& ecode);	

			void ScheduleIncompleteMessagesCleanup ();
			void HandleIncompleteMessagesCleanupTimer (const boost::system::error_code& ecode);	
    */	
			
		private:	

			SSUSession& m_Session;
			std::map<uint32_t, std::unique_ptr<InboundMessage> > m_IncompleteMessages;
			std::map<uint32_t, std::unique_ptr<OutboundMessage> > m_SentMessages;
			std::unordered_set<uint32_t> m_ReceivedMessages;
			int m_MaxPacketSize, m_PacketSize;
			i2p::I2NPMessagesHandler m_Handler;
			Time m_LastMessageReceivedTime; // since epoch
      Time m_LastTick; // in seconds
	};	
}
}

#endif

