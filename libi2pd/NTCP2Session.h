#ifndef LIBI2PD_NTCP2_SESSION_H
#define LIBI2PD_NTCP2_SESSION_H
#include "TransportSession.h"
#include "NTCP2.h"
#include "RouterInfo.h"
#include "Timestamp.h"
#include <memory>
#include <vector>

namespace i2p
{
namespace transport
{
  const size_t NTCP2_BUFFER_SIZE = 65537;

  struct NTCP2OptionBlock : public i2p::data::Tag<16>
  {
    NTCP2OptionBlock(uint16_t ver=2) : i2p::data::Tag<16>()
    {
      // zero all fields
      Zero();
      // put version
      uint8_t * ptr = *this;
      htobe16buf(ptr, ver);
    }

    void PutTimestamp(uint32_t ts)
    {
      uint8_t * ptr = *this;
      htobe32buf(ptr + 8, ts);
    }

    uint16_t Version() const
    {
      return buf16toh(data());
    }

    void PutPadLength(uint16_t l)
    {
      uint8_t * ptr = *this;
      htobe16buf(ptr+2, l);
    }

    uint16_t PadLength() const
    {
      return buf16toh(data() + 2);
    }

    void PutM3P2Length(uint16_t l)
    {
      uint8_t * ptr = *this;
      htobe16buf(ptr+4, l);
    }

    uint16_t M3P2Length() const
    {
      return buf16toh(data() + 4);
    }

    uint32_t Timestamp() const
    {
      return buf32toh(data() + 8);
    }

  };

  class NTCP2Session : 
    public TransportSession, 
    public std::enable_shared_from_this<NTCP2Session>
  {

    public:
      typedef i2p::data::RouterInfo RI;
      typedef std::shared_ptr<const RI> RI_cptr;

      typedef NTCP2Server::Socket_t Socket_t;
      typedef NTCP2Server::Service_t Service_t;
      typedef std::shared_ptr<I2NPMessage> I2NPMessage_ptr;
      typedef std::vector<I2NPMessage_ptr> I2NPMessageList;

      NTCP2Session(NTCP2Server& server, RI_cptr outbound=nullptr);
      NTCP2Session(NTCP2Session &) = delete;
      NTCP2Session(NTCP2Session &&) = delete;
      ~NTCP2Session();

      void Terminate();
      void Done();

      Socket_t & GetSocket() { return m_Socket; };
      Service_t & GetService();

      void ClientLogin();
      void ServerLogin();

      void SendI2NPMessages(const I2NPMessageList & msgs);

      bool IsEstablished () const { return m_IsEstablished; };
      bool IsTerminated () const { return m_IsTerminated; };

    private:
      void PostMessages(I2NPMessageList msgs);

    private:
      typedef I2NPMessageList SendQueue;

      NTCP2Server& m_Server;
      Socket_t m_Socket;

      bool m_IsEstablished, m_IsTerminated;

      NTCP2OptionBlock m_LocalOptions, m_RemoteOptions;

      i2p::crypto::AESAlignedBuffer<NTCP2_BUFFER_SIZE + 16> m_ReceiveBuffer;
      size_t m_ReceiveBufferOffset;

      I2NPMessage_ptr m_NextMessage;
      size_t m_NextMessageOffset;

      i2p::I2NPMessagesHandler m_Handler;

      bool m_IsSending;
      SendQueue m_SendQueue;
  };
}  
}

#endif