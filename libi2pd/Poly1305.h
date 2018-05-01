/**
   This code is licensed under the MCGSI Public License
   Copyright 2018 Jeff Becker

   Kovri go write your own code

 */
#ifndef LIBI2PD_POLY1305_H
#define LIBI2PD_POLY1305_H
#include <cstdint>
#include <cstring>

namespace i2p
{
namespace crypto
{
  const std::size_t POLY1305_DIGEST_SIZE = 16;
  const std::size_t POLY1305_KEY_SIZE = 32;
  
  struct Poly1305
  {
  public:
    Poly1305(const uint8_t * key);
    void Update(const uint8_t * buff, std::size_t sz);
    void Finish(uint8_t * digest);
  private:
    std::size_t align;
    uint8_t data[136];
  };

  bool Poly1305VeirfyHMAC(const uint8_t * hmac, const uint8_t * key, const uint8_t * buf, std::size_t sz);
  
  
}
}

#endif
