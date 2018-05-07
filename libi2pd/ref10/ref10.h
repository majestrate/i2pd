#ifndef LIBI2PD_REF10_H
#define LIBI2PD_REF10_h

namespace ref10 
{
  int scalarmult(unsigned char *q, const unsigned char *n, const unsigned char *p);
  int scalarmult_base(unsigned char *q, const unsigned char * n);
}

#endif