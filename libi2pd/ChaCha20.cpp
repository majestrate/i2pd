#include "ChaCha20.h"


namespace i2p
{
namespace crypto
{
namespace chacha20 
{
static inline void u32t8le(uint32_t v, uint8_t * p) 
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

static inline uint32_t u8t32le(const uint8_t * p) 
{
    uint32_t value = p[3];

    value = (value << 8) | p[2];
    value = (value << 8) | p[1];
    value = (value << 8) | p[0];

    return value;
}

static inline uint32_t rotl32(uint32_t x, int n) 
{
    return x << n | (x >> (-n & 31));
}

static inline void quarterround(uint32_t *x, int a, int b, int c, int d) 
{
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12);
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a],  8);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c],  7);
}
} // namespace chacha20

ChaCha20::ChaCha20(const uint8_t * key) : m_Counter(0)
{
    memcpy(m_Key, key, CHACHA20_KEY_SIZE);
}

void ChaCha20::Block(int rounds)
{
    int i;
    State_t x;
    x = m_State;

    for (i = rounds; i > 0; i -= 2) 
    {
        chacha20::quarterround(x, 0, 4,  8, 12);
        chacha20::quarterround(x, 1, 5,  9, 13);
        chacha20::quarterround(x, 2, 6, 10, 14);
        chacha20::quarterround(x, 3, 7, 11, 15);
        chacha20::quarterround(x, 0, 5, 10, 15);
        chacha20::quarterround(x, 1, 6, 11, 12);
        chacha20::quarterround(x, 2, 7,  8, 13);
        chacha20::quarterround(x, 3, 4,  9, 14);
    }

    m_Block << (x += m_State);
    ++m_State;
}

ChaCha20::State_t & ChaCha20::State_t::operator = (const ChaCha20::State_t & other)
{
    memcpy(this->data, other.data, sizeof(data));
    return *this;
}


ChaCha20::State_t & ChaCha20::State_t::operator += (const ChaCha20::State_t & other)
{
    uint32_t * us = *this;
    const uint32_t * them = other;
    for(int i = 0; i < 16; i++)
        us[i] += them[i];
    return *this;
}


ChaCha20::Block_t & ChaCha20::Block_t::operator << (const State_t & st)
{
    int i;
    const uint32_t * s = st;
    for (i = 0; i < 16; i++) 
        chacha20::u32t8le(s[i], *this + (i << 2));
    return *this;
}

void ChaCha20::Reset()
{
    m_Counter = 0;
}

void ChaCha20::BeforeXOR(const uint8_t * nonce)
{
    size_t i;

    m_State[0] = 0x61707865;
    m_State[1] = 0x3320646e;
    m_State[2] = 0x79622d32;
    m_State[3] = 0x6b206574;

    for (i = 0; i < 8; i++) 
        m_State[4 + i] = chacha20::u8t32le(m_Key + i * 4);
    

    m_State[12] = m_Counter;

    for (i = 0; i < 3; i++) 
        m_State[13 + i] = chacha20::u8t32le(nonce + i * 4);
}

void ChaCha20::XOR(uint8_t * dst, const uint8_t * src, const uint8_t * nonce, std::size_t sz)
{
    BeforeXOR(nonce);
    size_t i, j;

    for (i = 0; i < sz; i += CHACHA20_BLOCK_SIZE) 
    {
        Block();

        for (j = i; j < i + CHACHA20_BLOCK_SIZE; j++) 
        {
            if (j >= sz) break;
            dst[j] = src[j] ^ m_Block[j - i];
        }
    }
    ++m_Counter;
}

}
}