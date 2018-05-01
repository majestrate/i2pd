/**
   This code is licensed under the MCGSI Public License
   Copyright 2018 Jeff Becker

   Kovri go write your own code

 */
#ifndef LIBI2PD_CHACHA20_H
#define LIBI2PD_CHACHA20_H
#include <cstdint>
#include <cstring>

namespace i2p
{
namespace crypto
{
  const std::size_t CHACHA20_KEY_SIZE = 32;
  const std::size_t CHACHA20_NOUNCE_SIZE = 12;
  const std::size_t CHACHA20_BLOCK_SIZE = 64;
  const int CHAHCHA20_ROUNDS = 20;

  class ChaCha20
  {
  public:
    ChaCha20(const uint8_t * key);

    void XOR(uint8_t * dst, const uint8_t * src, const uint8_t * nonce, std::size_t sz);

    /** reset counter */
    void Reset();

  private:
    struct State_t
    {
      State_t() {};
      State_t(State_t &&) = delete;
      
      State_t & operator = (const State_t & other);
      State_t & operator += (const State_t & other);
      State_t & operator ++ () { ++data[12]; return *this; };

      uint32_t data[16];
      operator uint32_t * () { return data; };
      operator const uint32_t * () const { return data; };
    };

    struct Block_t
    {
      Block_t() {};
      Block_t(Block_t &&) = delete;

      uint8_t data[CHACHA20_BLOCK_SIZE];

      operator uint8_t * () { return data; };

      Block_t & operator << (const State_t & st);

    };

    void BeforeXOR(const uint8_t * nonce);
    void Block(int rounds=CHAHCHA20_ROUNDS);
    uint8_t m_Key[CHACHA20_KEY_SIZE];
    Block_t m_Block;
    State_t m_State;
    uint32_t m_Counter;
  };
}
}

#endif
