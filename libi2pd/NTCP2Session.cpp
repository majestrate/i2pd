#include "NTCP2Session.h"
#include "Curve25519.h"
#include "RouterContext.h"
#include "ChaCha20.h"
#include "Poly1305.h"
#include <openssl/aes.h>
#include <cassert>

namespace i2p
{
  namespace transport
  {
    const int NTCP2_SESSION_TIMEOUT = 30;

    NTCP2Session::NTCP2Session(NTCP2Server & server, RI_cptr router, std::shared_ptr<const RI::Address> remote) : 
      TransportSession(router, NTCP2_SESSION_TIMEOUT),
      m_Server(server), 
      m_Socket(server.GetService()),
      m_RemoteAddr(remote),
      m_IsEstablished(false), m_IsTerminated(false),
      m_BNCTX(BN_CTX_new())
    {
    }

    size_t NTCP2Session::GenerateSessionRequest(const std::string & protocolName)
    {
      // zero nonce
      m_Nonce.Zero();
      // space for 3 sha256 digests;
      uint8_t temp[96];
      // protocol name
      Hash(protocolName.c_str(), protocolName.size(), m_ChainingKey);
      // MixHash(null prologue)
      Hash(m_ChainingKey, 32, temp);
      // MixHash(null s)
      Hash(temp, 32, temp+32);
      // MixHash(null e)
      Hash(temp + 32, 32, temp);
     
      // remote set static key
      m_RemoteStaticKey = m_RemoteAddr->ntcp2->pubkey;
      
      // MixHash(rs)
      // TODO: verify EC point
      memcpy(temp+32, m_RemoteStaticKey, 32);
      Hash(temp, 64, temp + 64);
      // MixHash(null re)
      Hash(temp + 64, 32, temp);
        
      // generate seed for X via sha256(random)
      // use this buffer becuase we don't need it yet
      m_LocalEphemeralPubkey.Randomize();
      Hash(m_LocalEphemeralPubkey, 32, m_LocalEphemeralSeed);
      // calculate X
      i2p::crypto::curve25519::scalarmult_base(m_LocalEphemeralPubkey, m_LocalEphemeralSeed, m_BNCTX);
      // MixHash(e.pubkey)
      memcpy(temp + 32, m_LocalEphemeralPubkey, 32);
      Hash(temp, 64, temp+64);
      // store aead key
      memcpy(m_AEADKey, temp +64, 32);
      // calculate shared secret
      // input_key = DH()
      i2p::crypto::curve25519::scalarmult(temp, m_RemoteStaticKey, m_LocalEphemeralSeed, m_BNCTX);
      // temp_key = HMAC-SHA256(ck, input_key), temp_key is temp + 64
      i2p::crypto::HMACSHA256Digest(temp, 32, m_ChainingKey, temp + 64);
      // ck = HMAC-SH256(temp_key, 0x01)
      temp[0] = 1;
      i2p::crypto::HMACSHA256Digest(temp , 1, temp + 64, m_ChainingKey);
      // AEAD_key = HMAC-SHA256(temp_key, ck || byte(0x02)).
      memcpy(temp, m_ChainingKey, 32);
      temp[32] = 2;
      i2p::crypto::HMACSHA256Digest(temp, 33, temp + 64, m_AEADKey);

      // encrypt X with remote ident and IV
      {
        i2p::crypto::CBCEncryption aes;
        aes.SetKey(GetRemoteIdentity()->GetIdentHash());
        aes.SetIV(m_RemoteAddr->ntcp2->iv);
        aes.Encrypt(m_LocalEphemeralPubkey, 32, m_HandshakeSendBuffer);
      }
      // put options
      uint16_t padlen = rand() % 32;
      m_LocalOptions.PutPadLength(padlen);
      m_LocalOptions.PutTimestamp(i2p::util::GetSecondsSinceEpoch());
      // 48 + 32 + options (0) + padding 0-32 bytes
      m_LocalOptions.PutM3P2Length((rand() % 32) + 80 + context.GetIdentity()->GetFullLen());
      memcpy(m_HandshakeSendBuffer + 32, m_LocalOptions, 16);
      // fill random padding encrypt in place and hmac
      uint32_t hmac[4];
      RAND_bytes(m_HandshakeSendBuffer + 48, padlen);
      EncryptFrame(m_HandshakeSendBuffer + 32, 16 + padlen, hmac); 
      memcpy(m_HandshakeSendBuffer + 48 + padlen, hmac, 16);
      return 64 + padlen;
    }

    NTCP2Session::~NTCP2Session()
    {
      BN_CTX_free(m_BNCTX);
    }

    void NTCP2Session::Hash(const void * data, size_t sz, uint8_t * out)
    {
      SHA256_Init(&m_SHA);
      SHA256_Update(&m_SHA, data, sz);
      SHA256_Final(out, &m_SHA);
    }

    void NTCP2Session::Terminate()
    {

    }

    void NTCP2Session::ClientLogin()
    {
      GetService().post(std::bind(&NTCP2Session::SendSessionRequest, shared_from_this()));
    }

    void NTCP2Session::SendSessionRequest()
    {
      auto len = GenerateSessionRequest("Noise_XKaesobfse_25519_ChaChaPoly_SHA256");
      LogPrint(eLogDebug, "NTCP2Session: send Session Request ",len, " bytes");
      boost::asio::async_write(m_Socket, boost::asio::buffer(m_HandshakeSendBuffer, len), 
        boost::asio::transfer_all(), std::bind(&NTCP2Session::HandleSessionRequestSent, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
    }

    void NTCP2Session::EncryptFrame(uint8_t * buf, size_t sz, uint32_t * hmac)
    {
      i2p::crypto::chacha20(buf, sz, m_Nonce, m_AEADKey);
      i2p::crypto::Poly1305HMAC(hmac, m_AEADKey.word(), buf, sz);
    }

    void NTCP2Session::ServerLogin()
    {

    }

    void NTCP2Session::HandleSessionRequestSent(const error_t & err, size_t transferred)
    {
      auto remote = GetIdentHashBase64();
      if(err)
      {
        LogPrint(eLogError, "NTCP2Session: failed to send SessionRequest to ", remote, ": ", err.message());
        Terminate();
        return;
      }
      LogPrint(eLogDebug, "NTCP2Session: sent SessionRequest to ", remote);
      boost::asio::async_read(m_Socket, boost::asio::buffer(m_ReceiveBuffer, 46), std::bind(&NTCP2Session::HandleReadSessionCreated, shared_from_this(), std::placeholders::_1));
    }

    void NTCP2Session::HandleReadSessionCreated(const error_t & err)
    {
      auto remote = GetIdentHashBase64();
      if(err)
      {
        LogPrint(eLogError, "NTCP2Session: failed to read SessionCreated from ", remote, ": ", err.message());
        Terminate();
        return;
      }
      LogPrint(eLogDebug, "NTCP2Session: got first part of SessionCreated from ", remote);
    }

    void NTCP2Session::Done()
    {

    }

    NTCP2Session::Service_t & NTCP2Session::GetService()
    {
      return m_Server.GetService();
    }

    void NTCP2Session::SendI2NPMessages(const I2NPMessageList & msgs)
    {
      m_Server.GetService().post(std::bind(&NTCP2Session::PostMessages, shared_from_this(), msgs));
    }

    void NTCP2Session::PostMessages(I2NPMessageList msgs)
    {

    }

  }
}