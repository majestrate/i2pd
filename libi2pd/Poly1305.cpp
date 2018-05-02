#include "Poly1305.h"
#include "CPU.h"
#include <immintrin.h>

namespace i2p
{
namespace crypto
{
#if 0
#ifdef __AVX2__
	struct Poly1305_AVX2 
	{
		Poly1305_AVX2(const uint32_t *& k)
		{
			__asm__
			(
				"VMOVNTDQA %[key0], %%ymm0 \n"
				"VMOVNTDQA 32%[key0], %%ymm1 \n"
				:
				: 
				[key0]"m"(k)
			);
		};

		~Poly1305_AVX2()
		{	
			// clear out registers
			__asm__
			(
				"VZEROALL\n"
			);
		}

		void Update(const uint8_t * buf, size_t sz)
		{

		}

		void Finish(uint32_t *& out)
		{

		}



		size_t leftover;
		
	};
#endif
#endif
	namespace poly1305 
	{

		struct LongBlock
		{
			unsigned long data[17];
			operator unsigned long * ()
			{
				return data;
			}
		};

		struct Block
		{
			unsigned char data[17];

			operator uint8_t * ()
			{
				return data;
			}

			Block & operator += (const Block & other)
			{
				unsigned short u;
				unsigned int i;
				for(u = 0, i = 0; i < 17; i++)
				{
					u += (unsigned short) data[i] + (unsigned short) other.data[i];
					data[i] = (unsigned char) u & 0xff;
					u >>= 8;
				}
				return *this;
			}

			Block & operator %=(const LongBlock & other)
			{
				unsigned long u;
				unsigned int i;
				u = 0;
				for (i = 0; i < 16; i++) {
					u += other.data[i];
					data[i] = (unsigned char)u & 0xff;
					u >>= 8;
				}
				u += other.data[16];
				data[16] = (unsigned char)u & 0x03;
				u >>= 2;
				u += (u << 2);
				for (i = 0; i < 16; i++) {
					u += data[i];
					data[i] = (unsigned char)u & 0xff;
					u >>= 8;
				}
				data[16] += (unsigned char)u;
				return *this;
			}

			Block & operator = (const Block & other)
			{
				memcpy(data, other.data, sizeof(data));
				return *this;
			}

			Block & operator ~ ()
			{
				static const Block minusp = {
					0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
					0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
					0xfc
				};
				Block orig;
				unsigned char neg;
				unsigned int i;
				orig = *this;
				*this += minusp;
				neg = -(data[16] >> 7);
				for(i = 0; i < 17; i++)
					data[i] ^= neg & (orig.data[i] ^ data[i]);

				return *this;
			}

			void PutKey(const uint8_t * key)
			{
				data[0] = key[0] & 0xff;
				data[1] = key[1] & 0xff;
				data[2] = key[2] & 0xff;
				data[3] = key[3] & 0x0f;
				data[4] = key[4] & 0xfc;
				data[5] = key[5] & 0xff;
				data[6] = key[6] & 0xff;
				data[7] = key[7] & 0x0f;
				data[8] = key[8] & 0xfc;
				data[9] = key[9] & 0xff;
				data[10] = key[10] & 0xff;
				data[11] = key[11] & 0x0f;
				data[12] = key[12] & 0xfc;
				data[13] = key[13] & 0xff;
				data[14] = key[14] & 0xff;
				data[15] = key[15] & 0x0f;
				data[16] = 0;
			}

			void Put(const uint8_t * d, uint8_t last=0)
			{
				memcpy(data, d, 17);
				data[16] = last;
			}

			void Zero()
			{
				memset(data, 0, sizeof(data));
			}
		};

		struct Buffer
		{
			uint8_t data[POLY1305_BLOCK_BYTES];

			operator uint8_t * ()
			{
				return data;
			}
		};
	}

	struct Poly1305
	{

		Poly1305(const uint8_t * key) : m_Leftover(0), m_Final(0)
		{
			m_H.Zero();
			m_R.PutKey(key);
			m_Pad.Put(key + 16);
		}

		void Update(const uint8_t * buf, size_t sz)
		{
			// process leftover
			if(m_Leftover)
			{
				size_t want = POLY1305_BLOCK_BYTES - m_Leftover;
				if(want > sz) want = sz;
				memcpy(m_Buffer + m_Leftover, buf, want);
				sz -= want;
				buf += want;
				m_Leftover += want;
				if(m_Leftover < POLY1305_BLOCK_BYTES) return;
				Blocks(m_Buffer, POLY1305_BLOCK_BYTES);
				m_Leftover = 0;
			}
			// process blocks
			if(sz >= POLY1305_BLOCK_BYTES)
			{
				size_t want = (sz & ~(POLY1305_BLOCK_BYTES - 1));
				Blocks(buf, want);
				buf += want;
				sz -= want;
			}
			// leftover
			if(sz)
			{
				memcpy(m_Buffer+m_Leftover, buf, sz);
				m_Leftover += sz;
			}
		}

		void Blocks(const uint8_t * buf, size_t sz)
		{
			const unsigned char hi = m_Final ^ 1;
			poly1305::LongBlock hr;
			poly1305::Block c;
			while (sz >= POLY1305_BLOCK_BYTES) {
				
				unsigned long u;
				
				unsigned int i, j;
				c.Put(buf, hi);
				/* h += m */
				m_H += c;

				/* h *= r */
				for (i = 0; i < 17; i++) {
					u = 0;
					for (j = 0; j <= i ; j++) {
						u += (unsigned short)m_H.data[j] * m_R.data[i - j];
					}
					for (j = i + 1; j < 17; j++) {
						unsigned long v = (unsigned short)m_H.data[j] * m_R.data[i + 17 - j];
						v = ((v << 8) + (v << 6)); /* v *= (5 << 6); */
						u += v;
					}
					hr[i] = u;
				}
				/* (partial) h %= p */
				m_H %= hr;
				buf += POLY1305_BLOCK_BYTES;
				sz -= POLY1305_BLOCK_BYTES;
			}
		}

		void Finish(uint32_t *& out)
		{
			// process leftovers
			if(m_Leftover)
			{
				size_t idx = m_Leftover;
				m_Buffer[idx++] = 1;
				for(; idx < POLY1305_BLOCK_BYTES; idx++)
					m_Buffer[idx] = 0;
				m_Final = 1;
				Blocks(m_Buffer, POLY1305_BLOCK_BYTES);
			}

			// freeze H
			~m_H;
			// add pad
			m_H += m_Pad;
			// copy digest
			memcpy(out, m_H, 16);
			// clear state
			m_H.Zero();
			m_R.Zero();
			m_Pad.Zero();
		}

		size_t m_Leftover;
		poly1305::Buffer m_Buffer;
		poly1305::Block m_H;
		poly1305::Block m_R;
		poly1305::Block m_Pad;
		uint8_t m_Final;
		
	};

	void Poly1305HMAC(uint32_t * out, const uint32_t * key, const uint8_t * buf, std::size_t sz)
	{
		#if 0
		#ifdef __AVX2__
		if(i2p::cpu::avx2)
		{
			Poly1305_AVX2 p(key);
			p.Update(buf, sz);
			p.Finish(out);
		}
		else
		#endif
		#endif
		{
			const uint8_t * k = (const uint8_t *) key;
  			Poly1305 p(k);
  			p.Update(buf, sz);
   			p.Finish(out);
		}
  	}
}
}
