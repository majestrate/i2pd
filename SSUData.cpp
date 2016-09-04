#include <stdlib.h>
#include <boost/bind.hpp>
#include "Log.h"
#include "Timestamp.h"
#include "NetDb.h"
#include "SSU.h"
#include "SSUData.h"

namespace i2p
{
namespace transport
{

	SSUData::SSUData (SSUSession& session):
		m_Session (session), 
		m_MaxPacketSize (session.IsV6 () ? SSU_V6_MAX_PACKET_SIZE : SSU_V4_MAX_PACKET_SIZE), 
		m_PacketSize (m_MaxPacketSize), m_LastMessageReceivedTime (0)
	{
	}

	SSUData::~SSUData ()
	{
	}

	void SSUData::Start ()
	{
		//ScheduleIncompleteMessagesCleanup ();
	}	
		
	void SSUData::Stop ()
	{
		//m_ResendTimer.cancel ();
		//m_IncompleteMessagesCleanupTimer.cancel ();
	}	
		
	void SSUData::AdjustPacketSize (std::shared_ptr<const i2p::data::RouterInfo> remoteRouter)
	{
		if (remoteRouter) return;
		auto ssuAddress = remoteRouter->GetSSUAddress ();
		if (ssuAddress && ssuAddress->mtu)
		{
			if (m_Session.IsV6 ())
				m_PacketSize = ssuAddress->mtu - IPV6_HEADER_SIZE - UDP_HEADER_SIZE;
			else
				m_PacketSize = ssuAddress->mtu - IPV4_HEADER_SIZE - UDP_HEADER_SIZE;
			if (m_PacketSize > 0)
			{
				// make sure packet size multiple of 16
				m_PacketSize >>= 4;
				m_PacketSize <<= 4;
				if (m_PacketSize > m_MaxPacketSize) m_PacketSize = m_MaxPacketSize;
				LogPrint (eLogDebug, "SSU: MTU=", ssuAddress->mtu, " packet size=", m_PacketSize);
			}
			else
			{	
				LogPrint (eLogWarning, "SSU: Unexpected MTU ", ssuAddress->mtu);
				m_PacketSize = m_MaxPacketSize;
			}	
		}		
	}

	void SSUData::UpdatePacketSize (const i2p::data::IdentHash& remoteIdent)
	{
 		auto routerInfo = i2p::data::netdb.FindRouter (remoteIdent);
		if (routerInfo)
			AdjustPacketSize (routerInfo);
	}

	void SSUData::ProcessSentMessageAck (uint32_t msgID)
	{
		auto it = m_SentMessages.find (msgID);
		if (it != m_SentMessages.end ())
		{
			m_SentMessages.erase (it);
		}
	}		

	void SSUData::ProcessAcks (uint8_t *& buf, uint8_t flag)
	{
		if (flag & DATA_FLAG_EXPLICIT_ACKS_INCLUDED)
		{
			// explicit ACKs
			uint8_t numAcks =*buf;
			buf++;
			for (int i = 0; i < numAcks; i++)
				ProcessSentMessageAck (bufbe32toh (buf+i*4));
			buf += numAcks*4;
		}
		if (flag & DATA_FLAG_ACK_BITFIELDS_INCLUDED)
		{
			// explicit ACK bitfields
			uint8_t numBitfields =*buf;
			buf++;
			for (int i = 0; i < numBitfields; i++)
			{
				uint32_t msgID = bufbe32toh (buf);
				buf += 4; // msgID
				auto it = m_SentMessages.find (msgID);		
				// process individual Ack bitfields
				bool isNonLast = false;
				int fragment = 0;
				do
				{
					uint8_t bitfield = *buf;
					isNonLast = bitfield & 0x80;
					bitfield &= 0x7F; // clear MSB
					if (bitfield && it != m_SentMessages.end ())
					{	
						int numSentFragments = it->second->fragments.size ();		
						// process bits
						uint8_t mask = 0x01;
						for (int j = 0; j < 7; j++)
						{			
							if (bitfield & mask)
							{
								if (fragment < numSentFragments)
									it->second->fragments[fragment].reset (nullptr);
							}				
							fragment++;
							mask <<= 1;
						}
					}	
					buf++;
				}
				while (isNonLast); 
			}	
		}		
	}

	void SSUData::ProcessFragments (uint8_t * buf)
	{
		uint8_t numFragments = *buf; // number of fragments
		buf++;
		for (int i = 0; i < numFragments; i++)
		{	
			uint32_t msgID = bufbe32toh (buf); // message ID
			buf += 4;
			uint8_t frag[4];
			frag[0] = 0;
			memcpy (frag + 1, buf, 3);
			buf += 3;
			uint32_t fragmentInfo = bufbe32toh (frag); // fragment info
			uint16_t fragmentSize = fragmentInfo & 0x3FFF; // bits 0 - 13
			bool isLast = fragmentInfo & 0x010000; // bit 16	
			uint8_t fragmentNum = fragmentInfo >> 17; // bits 23 - 17 		
			if (fragmentSize >= SSU_V4_MAX_PACKET_SIZE)
			{
				LogPrint (eLogError, "SSU: Fragment size ", fragmentSize, " exceeds max SSU packet size");
				return;
			}

			//  find message with msgID
			auto it = m_IncompleteMessages.find (msgID);
			if (it == m_IncompleteMessages.end ()) 
			{
				// create new message
				auto msg = NewI2NPShortMessage ();
				msg->len -= I2NP_SHORT_HEADER_SIZE;
				it = m_IncompleteMessages.insert (std::make_pair (msgID, 
					std::unique_ptr<InboundMessage>(new InboundMessage (msg)))).first;
			}
			std::unique_ptr<InboundMessage>& incompleteMessage = it->second;

			// handle current fragment
			if (fragmentNum == incompleteMessage->nextFragmentNum)
			{
				// expected fragment
				incompleteMessage->AttachNextFragment (buf, fragmentSize);
				if (!isLast && !incompleteMessage->savedFragments.empty ())
				{
					// try saved fragments
					for (auto it1 = incompleteMessage->savedFragments.begin (); it1 != incompleteMessage->savedFragments.end ();)
					{
						auto& savedFragment = *it1;
						if (savedFragment->fragmentNum == incompleteMessage->nextFragmentNum)
						{
							incompleteMessage->AttachNextFragment (savedFragment->buf, savedFragment->len);
							isLast = savedFragment->isLast;
							incompleteMessage->savedFragments.erase (it1++);
						}
						else
							break;
					}
					if (isLast)
						LogPrint (eLogDebug, "SSU: Message ", msgID, " complete");
				}	
			}	
			else
			{	
				if (fragmentNum < incompleteMessage->nextFragmentNum)
					// duplicate fragment
					LogPrint (eLogWarning, "SSU: Duplicate fragment ", (int)fragmentNum, " of message ", msgID, ", ignored");
				else
				{
					// missing fragment
					LogPrint (eLogWarning, "SSU: Missing fragments from ", (int)incompleteMessage->nextFragmentNum, " to ", fragmentNum - 1, " of message ", msgID);
					auto savedFragment = new Fragment (fragmentNum, buf, fragmentSize, isLast);
					if (incompleteMessage->savedFragments.insert (std::unique_ptr<Fragment>(savedFragment)).second)
						incompleteMessage->lastFragmentInsertTime = i2p::util::GetSinceEpoch<Time> ();
					else	
						LogPrint (eLogWarning, "SSU: Fragment ", (int)fragmentNum, " of message ", msgID, " already saved");
				}
				isLast = false;
			}	

			if (isLast)
			{
				// delete incomplete message
				auto msg = incompleteMessage->msg;
				incompleteMessage->msg = nullptr;
				m_IncompleteMessages.erase (msgID);				
				// process message
				SendMsgAck (msgID);
				msg->FromSSU (msgID);
				if (m_Session.GetState () == eSessionStateEstablished)
				{
					if (!m_ReceivedMessages.count (msgID))
					{	
						m_ReceivedMessages.insert (msgID);
						m_LastMessageReceivedTime = i2p::util::GetSinceEpoch<Time> ();
						if (!msg->IsExpired ())
							m_Handler.PutNextMessage (msg);
						else
							LogPrint (eLogDebug, "SSU: message expired");
					}	
					else
						LogPrint (eLogWarning, "SSU: Message ", msgID, " already received");
				}	
				else
				{
					// we expect DeliveryStatus
					if (msg->GetTypeID () == eI2NPDeliveryStatus)
					{
						LogPrint (eLogDebug, "SSU: session established");
						m_Session.Established ();
					}	
					else
						LogPrint (eLogError, "SSU: unexpected message ", (int)msg->GetTypeID ());
				}	
			}	
			else
				SendFragmentAck (msgID, fragmentNum);			
			buf += fragmentSize;
		}	
	}

	void SSUData::FlushReceivedMessage ()
	{
		m_Handler.Flush ();
	}	
		
	void SSUData::ProcessMessage (uint8_t * buf, size_t len)
	{
		//uint8_t * start = buf;
		uint8_t flag = *buf;
		buf++;
		LogPrint (eLogDebug, "SSU: Process data, flags=", (int)flag, ", len=", len);
		// process acks if presented
		if (flag & (DATA_FLAG_ACK_BITFIELDS_INCLUDED | DATA_FLAG_EXPLICIT_ACKS_INCLUDED))
			ProcessAcks (buf, flag);
		// extended data if presented
		if (flag & DATA_FLAG_EXTENDED_DATA_INCLUDED)
		{
			uint8_t extendedDataSize = *buf;
			buf++; // size
			LogPrint (eLogDebug, "SSU: extended data of ", extendedDataSize, " bytes present");
			buf += extendedDataSize;
		}
		// process data
		ProcessFragments (buf);
	}

	void SSUData::Send (std::shared_ptr<i2p::I2NPMessage> msg)
	{
		uint32_t msgID = msg->ToSSU ();
		if (m_SentMessages.count (msgID) > 0)
		{
			LogPrint (eLogWarning, "SSU: message ", msgID, " already sent");
			return;
		}
    /*
		if (m_SentMessages.empty ()) // schedule resend at first message only
			ScheduleResend ();
    */
		auto ret = m_SentMessages.insert (std::make_pair (msgID, std::unique_ptr<OutboundMessage>(new OutboundMessage))); 
		std::unique_ptr<OutboundMessage>& sentMessage = ret.first->second;
		if (ret.second)	
		{
			sentMessage->nextResendTime = i2p::util::GetSinceEpoch<Time> () + RESEND_INTERVAL;
			sentMessage->numResends = 0;
		}	
		auto& fragments = sentMessage->fragments;
		size_t payloadSize = m_PacketSize - sizeof (SSUHeader) - 9; // 9  =  flag + #frg(1) + messageID(4) + frag info (3) 
		size_t len = msg->GetLength ();
		uint8_t * msgBuf = msg->GetSSUHeader ();

		uint32_t fragmentNum = 0;
		while (len > 0)
		{	
			Fragment * fragment = new Fragment;
			fragment->fragmentNum = fragmentNum;
			uint8_t * buf = fragment->buf;
			uint8_t	* payload = buf + sizeof (SSUHeader);
			*payload = DATA_FLAG_WANT_REPLY; // for compatibility
			payload++;
			*payload = 1; // always 1 message fragment per message
			payload++;
			htobe32buf (payload, msgID);
			payload += 4;
			bool isLast = (len <= payloadSize);
			size_t size = isLast ? len : payloadSize;
			uint32_t fragmentInfo = (fragmentNum << 17);
			if (isLast)
				fragmentInfo |= 0x010000;
			
			fragmentInfo |= size;
			fragmentInfo = htobe32 (fragmentInfo);
			memcpy (payload, (uint8_t *)(&fragmentInfo) + 1, 3);
			payload += 3;
			memcpy (payload, msgBuf, size);
			
			size += payload - buf;
			if (size & 0x0F) // make sure 16 bytes boundary
				size = ((size >> 4) + 1) << 4; // (/16 + 1)*16
			fragment->len = size; 
			fragments.push_back (std::unique_ptr<Fragment> (fragment));
			
			// encrypt message with session key
			m_Session.FillHeaderAndEncrypt (PAYLOAD_TYPE_DATA, buf, size);
			try
			{	
				m_Session.Send (buf, size);
			}
			catch (boost::system::system_error& ec)
			{
				LogPrint (eLogWarning, "SSU: Can't send data fragment ", ec.what ());
			}	
			if (!isLast)
			{	
				len -= payloadSize;
				msgBuf += payloadSize;
			}	
			else
				len = 0;
			fragmentNum++;
		}	
	}		

	void SSUData::SendMsgAck (uint32_t msgID)
	{
		uint8_t buf[48 + 18]; // actual length is 44 = 37 + 7 but pad it to multiple of 16
		uint8_t * payload = buf + sizeof (SSUHeader);
		*payload = DATA_FLAG_EXPLICIT_ACKS_INCLUDED; // flag
		payload++;
		*payload = 1; // number of ACKs
		payload++;
		htobe32buf (payload, msgID); // msgID
		payload += 4;
		*payload = 0; // number of fragments

		// encrypt message with session key
		m_Session.FillHeaderAndEncrypt (PAYLOAD_TYPE_DATA, buf, 48);
		m_Session.Send (buf, 48);
	}

	void SSUData::SendFragmentAck (uint32_t msgID, int fragmentNum)
	{
		if (fragmentNum > 64)
		{
			LogPrint (eLogWarning, "SSU: Fragment number ", fragmentNum, " exceeds 64");
			return;
		}
		uint8_t buf[64 + 18];
		uint8_t * payload = buf + sizeof (SSUHeader);
		*payload = DATA_FLAG_ACK_BITFIELDS_INCLUDED; // flag
		payload++;	
		*payload = 1; // number of ACK bitfields
		payload++;
		// one ack
		*(uint32_t *)(payload) = htobe32 (msgID); // msgID	
		payload += 4;
		div_t d = div (fragmentNum, 7);
		memset (payload, 0x80, d.quot); // 0x80 means non-last
		payload += d.quot;		
		*payload = 0x01 << d.rem; // set corresponding bit
		payload++;
		*payload = 0; // number of fragments

		size_t len = d.quot < 4 ? 48 : 64; // 48 = 37 + 7 + 4 (3+1)			
		// encrypt message with session key
		m_Session.FillHeaderAndEncrypt (PAYLOAD_TYPE_DATA, buf, len);
		m_Session.Send (buf, len);
	}	


  bool SSUData::Tick(const Time now)
  {
    // handle inbound cleanup
    // for each inbound message ...
    for ( auto it = m_IncompleteMessages.begin (); it != m_IncompleteMessages.end(); )
    {
      // has this message not gotten a fragment in time?
      if ( now > it->second->lastFragmentInsertTime + INCOMPLETE_MESSAGES_CLEANUP_TIMEOUT)
      {
        // recveive has timed out so delete the message from inbound queue
        LogPrint(eLogInfo, "SSU: message ", it->first, " was not received in ",
                 std::chrono::duration_cast<std::chrono::seconds>(INCOMPLETE_MESSAGES_CLEANUP_TIMEOUT).count(),
                 " seconds, deleted");
        it = m_IncompleteMessages.erase(it);
      }
      else // it has not timed out yet
        ++it;
    }

    // decay inbound message queue
    if (m_ReceivedMessages.size() > MAX_NUM_RECEIVED_MESSAGES || now > m_LastMessageReceivedTime + DECAY_INTERVAL)
      m_ReceivedMessages.clear();

    
    // handle outbound resend
    size_t numResends = 0;
    // for each message we are sending  ...
    for ( auto it = m_SentMessages.begin(); it != m_SentMessages.end(); )
    {
      if ( now >= it->second->nextResendTime)
      {
        // we need to resend this message
        if (it->second->numResends < MAX_NUM_RESENDS)
        {
          // we have not exceeded max number of resend tries yet
          // for each fragment ...
          for ( const auto & frag : it->second->fragments)
          {
            if(frag)
            {
              try
              {
                m_Session.Send(frag->buf, frag->len); // try resending fragment
              }
              catch (boost::system::system_error & ec)
              {
                // catch exception from resend
                LogPrint(eLogWarning, "SSU: can't resend data fragment ", ec.what());
              }
            }
          } // end for
          // increment resend attempts
          it->second->numResends ++;
          // linear backoff for resend
          it->second->nextResendTime += it->second->numResends * RESEND_INTERVAL;
          // next message
          ++it;
        }
        else
        {
          // max resend attempts reached for this message
          LogPrint(eLogInfo, "SSU: message not ACKed after ", MAX_NUM_RESENDS, " attempts, deleted");
          // erase message from send queue
          it = m_SentMessages.erase(it);
        }
      }
      else
        ++it; // no resend required for this message
    }
    m_LastTick = now;
    return numResends < MAX_OUTGOING_WINDOW_SIZE;  
	}	
}
}

