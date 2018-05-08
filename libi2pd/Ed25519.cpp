#include "EdDSA.h"
#include "CPU.h"

#ifdef __AVX2__
#include "sandy2x/sandy2x.h"
#endif

namespace i2p
{
namespace crypto
{
	namespace curve25519
	{	

		void scalarmult(uint8_t * q, const uint8_t * n, const uint8_t * p)
		{
#ifdef __AVX2__
			if(i2p::cpu::avx2)
			{
				sandy2x::scalarmult(q, n, p);
			}
			else
#endif
			{
				GetEd25519()->ScalarMult(q, n, p);
			}
		}

		void scalarmult_base(uint8_t * q, const uint8_t * n)
		{
#ifdef __AVX2__
			if(i2p::cpu::avx2)
			{
				sandy2x::scalarmult_base(q, n);
			}
			else
#endif
			{
				GetEd25519()->ScalarMultBase(q, n);
			}
		}
	}

	static std::unique_ptr<Ed25519> g_Ed25519;
	std::unique_ptr<Ed25519>& GetEd25519 ()
	{
		if (!g_Ed25519)
		{
			auto c = new Ed25519();
			if (!g_Ed25519) // make sure it was not created already
				g_Ed25519.reset (c);
			else
				delete c;
		}
		return g_Ed25519;
	}

}
}
