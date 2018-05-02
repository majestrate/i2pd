#include "EdDSA.h"
namespace i2p
{
namespace crypto
{
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
