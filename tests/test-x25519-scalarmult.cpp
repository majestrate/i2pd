#include "Curve25519.h"
#include "CPU.h"
#include "Crypto.h"

#include <iomanip>
#include <iostream>

void dumphex(const uint8_t * hex8, size_t sz)
{
    std::cerr << std::hex << std::setw(2);
    size_t idx = 0;
    while(idx < sz)
        std::cerr << (int) hex8[idx++] << " ";
    std::cerr << std::endl;
}

uint8_t nibble(char ch)
{
  if(ch > 96 && ch < 103)
    return ch - 87;
  else if (ch > 47 || ch < 58)
    return ch - 48;
  else 
    abort();
}

void PutValue(uint8_t * buf, const char * str)
{
  auto slen = strlen(str);
  size_t idx = 0;
  while(idx < slen)
  {
    buf[idx/2] = ( nibble(str[idx]) << 4 ) | nibble(str[idx+1]);
    idx += 2;
  }
}

int main(int, char *[])
{
  uint8_t scalar[32];
  PutValue(scalar, "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
  uint8_t coord[32];
  PutValue(coord, "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
  uint8_t expected[32];
  PutValue(expected, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
  uint8_t buf[32];
  i2p::cpu::Detect();
  i2p::crypto::InitCrypto(false);
  i2p::crypto::curve25519::scalarmult(buf, scalar, coord);
  auto result = memcmp(buf, expected, 32);
  if(result)
  {
    std::cout << "got " ;
    dumphex(buf, 32);
    std::cout << "expected ";
    dumphex(expected, 32);
    return 1;
  }
  return 0;
}