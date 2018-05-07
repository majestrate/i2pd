#include "EdDSA.h"
#include "CPU.h"

#ifdef __AVX2__
#include "sandy2x/sandy2x.h"
#endif
#include "ref10/ref10.h"


namespace i2p
{
namespace crypto
{
	namespace x25519 
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
				ref10::scalarmult(q, n, p);
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
				ref10::scalarmult_base(q, n);
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
