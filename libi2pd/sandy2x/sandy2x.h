#ifndef SANDY2X_H
#define SANDY2X_H
#include "fe.h"
#include "fe51.h"
#include "ladder.h"

namespace sandy2x
{
  int scalarmult(unsigned char *q, const unsigned char *n, const unsigned char *p);
  int scalarmult_base(unsigned char * q, const unsigned char * n);
}

#endif