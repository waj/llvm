//===-- Use.cpp - Implement the Use class -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the algoritm for finding the User of a Use.
//
//===----------------------------------------------------------------------===//

#include "llvm/Value.h"

namespace llvm {

//===----------------------------------------------------------------------===//
//                         Use getImpliedUser Implementation
//===----------------------------------------------------------------------===//

const Use *Use::getImpliedUser() const {
  bool StopEncountered = false;
  ptrdiff_t Offset = 0;
  const Use *Current = this;
  enum { stop = 0x2, fullstop = 0x3 };

  while (true) {
    unsigned Tag = unsigned(Current->Val) & 0x3;
    switch (Tag)
      {
      case 0:
      case 1:   // digits
	if (StopEncountered)
	  Offset = (Offset << 1) + Tag;
	break;
      case stop:
	if (StopEncountered)
	  return Current + Offset;
	StopEncountered = true;
	break;
      case fullstop:
	return Current + 1;
      }

    ++Current;
  }
}
}
