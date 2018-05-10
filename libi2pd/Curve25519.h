#ifndef LIBI2PD_CURVE25519_H
#define LIBI2PD_CURVE25519_H
#include <cstdint>
#include <openssl/bn.h>

namespace i2p
{
  namespace crypto
  {

    const std::size_t CURVE25519_KEY_LENGTH = 32;

    namespace curve25519
    {
      /** curve25519 scalarmult */
      void scalarmult(uint8_t * q, const uint8_t * n, const uint8_t * p, BN_CTX * ctx);
      /** curve25519 scalarmult base */
      void scalarmult_base(uint8_t * q, const uint8_t * n, BN_CTX * ctx);
    }
  }
}

#endif