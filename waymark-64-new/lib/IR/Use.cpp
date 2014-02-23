//===-- Use.cpp - Implement the Use class ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the algorithm for finding the User of a Use.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Value.h"
#include <new>

namespace llvm {

//===----------------------------------------------------------------------===//
//                         Use swap Implementation
//===----------------------------------------------------------------------===//

void Use::swap(Use &RHS) {
  Value *V1(Val);
  Value *V2(RHS.Val);
  if (V1 != V2) {
    if (V1) {
      removeFromList();
    }

    if (V2) {
      RHS.removeFromList();
      Val = V2;
      V2->addUse(*this);
    } else {
      Val = 0;
    }

    if (V1) {
      RHS.Val = V1;
      V1->addUse(RHS);
    } else {
      RHS.Val = 0;
    }
  }
}

//===----------------------------------------------------------------------===//
//                         Use getImpliedUser Implementation
//===----------------------------------------------------------------------===//

/* NEED SFINAE tricks: http://stackoverflow.com/questions/14603163/how-to-use-sfinae-for-selecting-constructors
template<>
const Use *Use::getImpliedUser<4>() const {
  const Use *Current = this;

  while (true) {
    switch (unsigned Tag = (Current++)->Prev.getInt()) {
      case zeroDigitTag:
      case oneDigitTag:
        continue;

      case stopTag: {
        ++Current;
        ptrdiff_t Offset = 1;
        while (true) {
          switch (unsigned Tag = Current->Prev.getInt()) {
            case zeroDigitTag:
            case oneDigitTag:
              ++Current;
              Offset = (Offset << 1) + Tag;
              continue;
            default:
              return Current + Offset;
          }
        }
      }

      case fullStopTag:
        return Current;
    }
  }
}


enum Tag3 {
  fullStopTag3,
  stopTag3,
  skipStopTag3,
  skip2StopTag3,
  zeroZeroDigitTag3,
  zeroOneDigitTag3,
  oneZeroDigitTag3,
  oneOneDigitTag3
};
*/

template<>
const Use *Use::getImpliedUser<8>() const {
  const Use *Current = this;

  while (true) {
    switch (unsigned Tag = Current->Prev.getInt()) {
      case fullStopTag3: return Current + 1;
      case stopTag3:
      case skipStopTag3:
      case skip2StopTag3: {
        Current += Tag;
        ptrdiff_t Offset = 0;
        while (true) {
          switch (unsigned Tag = Current->Prev.getInt()) {
            case zeroZeroDigitTag3:
            case zeroOneDigitTag3:
            case oneZeroDigitTag3:
            case oneOneDigitTag3:
              ++Current;
              Offset = (Offset << 2) | (Tag & 0x3);
              continue;
            default:
              return Current + Offset;
          }
        }
      }

      default: Current += 3; continue;
    }
  }
}

//===----------------------------------------------------------------------===//
//                         Use initTags Implementation
//===----------------------------------------------------------------------===//

/*
template<>
Use *Use::initTags<4>(Use * const Start, Use *Stop) {
  ptrdiff_t Done = 0;
  while (Done < 32) {
    if (Start == Stop--)
      return Start;
#   define TAG_AT(N, TAG) (uintptr_t(TAG ## Tag) << ((N) * 2))
    static const uintptr_t tags =
      TAG_AT(0, fullStop) | TAG_AT(1, oneDigit) | TAG_AT(2, stop) |
      TAG_AT(3, oneDigit) | TAG_AT(4, oneDigit) | TAG_AT(5, stop) |
      TAG_AT(6, zeroDigit) | TAG_AT(7, oneDigit) | TAG_AT(8, oneDigit) |
      TAG_AT(9, stop) | TAG_AT(10, zeroDigit) | TAG_AT(11, oneDigit) |
      TAG_AT(12, zeroDigit) | TAG_AT(13, oneDigit) | TAG_AT(14, stop) |
      TAG_AT(15, oneDigit) | TAG_AT(16, oneDigit) | TAG_AT(17, oneDigit) |
      TAG_AT(18, oneDigit) | TAG_AT(19, stop) |
      TAG_AT(20, zeroDigit) | TAG_AT(21, zeroDigit) | TAG_AT(22, oneDigit) |
      TAG_AT(23, zeroDigit) | TAG_AT(24, oneDigit) | TAG_AT(25, stop) |
      TAG_AT(26, zeroDigit) | TAG_AT(27, oneDigit) | TAG_AT(28, zeroDigit) |
      TAG_AT(29, oneDigit) | TAG_AT(30, oneDigit) | TAG_AT(31, stop);
#   undef TAG_AT
    new(Stop) Use(PrevPtrTag((tags >> Done++ * 2) & 0x3));
  }

  ptrdiff_t Count = Done;
  while (Start != Stop) {
    --Stop;
    if (!Count) {
      new(Stop) Use(stopTag);
      ++Done;
      Count = Done;
    } else {
      new(Stop) Use(PrevPtrTag(Count & 1));
      Count >>= 1;
      ++Done;
    }
  }

  return Start;
}
*/

template<>
Use *Use::initTags<8>(Use * const Start, Use *Stop) {
  ptrdiff_t Done = 0;
  while (Done < 17) {
    if (Start == Stop--)
      return Start;
#   define TAG_AT(N, TAG) (uintptr_t(TAG ## Tag3) << ((N) * 3))
    static const uintptr_t tags =
      TAG_AT(0, fullStop) | TAG_AT(1, stop) | TAG_AT(2, skipStop) |
      TAG_AT(3, oneOneDigit) | TAG_AT(4, stop) | TAG_AT(5, skipStop) |
      TAG_AT(6, skip2Stop) | TAG_AT(7, oneOneDigit) | TAG_AT(8, zeroOneDigit) |
      TAG_AT(9, stop) | TAG_AT(10, skipStop) | TAG_AT(11, skip2Stop) |
      TAG_AT(12, zeroZeroDigit) | TAG_AT(13, oneOneDigit) | TAG_AT(14, stop) |
      TAG_AT(15, skipStop) | TAG_AT(16, skip2Stop);
#   undef TAG_AT
    new(Stop) Use(Tag_t((tags >> Done++ * 2) & 0x3));
  }

  ptrdiff_t Count = Done;
  while (Start != Stop) {
    --Stop;
    if (!Count) {
      new(Stop) Use(stopTag3);
      ++Done;
      Count = Done;
      if (Start == Stop) return Start;
      --Stop;
      new(Stop) Use(skipStopTag3);
      ++Done;
      Count = Done;
      if (Start == Stop) return Start;
      --Stop;
      new(Stop) Use(skip2StopTag3);
      ++Done;
      Count = Done;
      if (Start == Stop) return Start;
    } else {
      new(Stop) Use(Tag_t(zeroOneDigitTag3 | (Count & 0x3)));
      Count >>= 2;
      ++Done;
    }
  }

  return Start;
}


//===----------------------------------------------------------------------===//
//                         Use zap Implementation
//===----------------------------------------------------------------------===//

void Use::zap(Use *Start, const Use *Stop, bool del) {
  while (Start != Stop)
    (--Stop)->~Use();
  if (del)
    ::operator delete(Start);
}

//===----------------------------------------------------------------------===//
//                         Use getUser Implementation
//===----------------------------------------------------------------------===//

User *Use::getUser() const {
  const Use *End = getImpliedUser();
  const UserRef *ref = reinterpret_cast<const UserRef*>(End);
  return ref->getInt()
    ? ref->getPointer()
    : reinterpret_cast<User*>(const_cast<Use*>(End));
}

} // End llvm namespace
