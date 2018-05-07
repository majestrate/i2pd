#ifndef LIBI2PD_X25519_H
#define LIBI2PD_X25519_H

#include <openssl/bn.h>
#include <memory>

namespace i2p
{
  namespace crypto
  {

  const size_t EDDSA25519_PUBLIC_KEY_LENGTH = 32;
	const size_t EDDSA25519_SIGNATURE_LENGTH = 64;
	const size_t EDDSA25519_PRIVATE_KEY_LENGTH = 32;

  namespace x25519
  {
    /** x25519 scalarmult */
    void scalarmult(uint8_t * q, const uint8_t * n, const uint8_t * p);
    /** x25519 scalarmult base */
    void scalarmult_base(uint8_t * q, const uint8_t * n);
  }

  // EdDSA point
	struct EDDSAPoint
	{
		BIGNUM * x {nullptr};
		BIGNUM * y {nullptr};
		BIGNUM * z {nullptr};
		BIGNUM * t {nullptr}; // projective coordinates

		EDDSAPoint () {}
		EDDSAPoint (const EDDSAPoint& other)   { *this = other; }
		EDDSAPoint (EDDSAPoint&& other)        { *this = std::move (other); }
		EDDSAPoint (BIGNUM * x1, BIGNUM * y1, BIGNUM * z1 = nullptr, BIGNUM * t1 = nullptr)
			: x(x1)
			, y(y1)
			, z(z1)
			, t(t1)
		{}
		~EDDSAPoint () { BN_free (x); BN_free (y); BN_free(z); BN_free(t); }

		EDDSAPoint& operator=(EDDSAPoint&& other)
		{
			if (this != &other)
			{
				BN_free (x); x = other.x; other.x = nullptr;
				BN_free (y); y = other.y; other.y = nullptr;
				BN_free (z); z = other.z; other.z = nullptr;
				BN_free (t); t = other.t; other.t = nullptr;
			}
			return *this;
		}

		EDDSAPoint& operator=(const EDDSAPoint& other)
		{
			if (this != &other)
			{
				BN_free (x); x = other.x ? BN_dup (other.x) : nullptr;
				BN_free (y); y = other.y ? BN_dup (other.y) : nullptr;
				BN_free (z); z = other.z ? BN_dup (other.z) : nullptr;
				BN_free (t); t = other.t ? BN_dup (other.t) : nullptr;
			}
			return *this;
		}

		EDDSAPoint operator-() const
		{
			BIGNUM * x1 = NULL, * y1 = NULL, * z1 = NULL, * t1 = NULL;
			if (x) { x1 = BN_dup (x); BN_set_negative (x1, !BN_is_negative (x)); };
			if (y) y1 = BN_dup (y);
			if (z) z1 = BN_dup (z);
			if (t) { t1 = BN_dup (t); BN_set_negative (t1, !BN_is_negative (t)); };
			return EDDSAPoint {x1, y1, z1, t1};
		}
	};
  }
}

#endif