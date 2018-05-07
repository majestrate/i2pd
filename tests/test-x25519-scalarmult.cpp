#include "X25519.h"
#include "CPU.h"

int main(int, char *[])
{
  uint8_t scalar[64];
  uint8_t result[32];
  uint8_t us[32];
  i2p::cpu::Detect();
  i2p::crypto::x25519::scalarmult(result, us, scalar);
  return 0;
}