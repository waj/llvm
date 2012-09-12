//===-- AMDILUtilityFunctions.h -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides declarations for functions that are used across
// different classes and provide various conversions or utility to shorten the code.
//
//===----------------------------------------------------------------------===//

#ifndef AMDILUTILITYFUNCTIONS_H_
#define AMDILUTILITYFUNCTIONS_H_

#include "AMDIL.h"
#include "AMDILLLVMVersion.h"
#include "AMDILTargetMachine.h"
#include "llvm/ADT/SmallVector.h"

// Utility functions from ID
//
namespace llvm {
class TargetRegisterClass;
class TargetMachine;
class SDValue;
class SDNode;
class Value;
class Type;
class TypeSymbolTable;
class StructType;
class IntegerType;
class FunctionType;
class VectorType;
class ArrayType;
class PointerType;
class OpaqueType;
class MachineInstr;
class AMDILMachineFunctionInfo;
}
enum SrcSwizzles {
  AMDIL_SRC_DFLT = 0,
  AMDIL_SRC_X000,
  AMDIL_SRC_0X00,
  AMDIL_SRC_00X0,
  AMDIL_SRC_000X,
  AMDIL_SRC_Y000,
  AMDIL_SRC_0Y00,
  AMDIL_SRC_00Y0,
  AMDIL_SRC_000Y,
  AMDIL_SRC_Z000,
  AMDIL_SRC_0Z00,
  AMDIL_SRC_00Z0,
  AMDIL_SRC_000Z,
  AMDIL_SRC_W000,
  AMDIL_SRC_0W00,
  AMDIL_SRC_00W0,
  AMDIL_SRC_000W,
  AMDIL_SRC_XY00,
  AMDIL_SRC_00XY,
  AMDIL_SRC_ZW00,
  AMDIL_SRC_00ZW,
  AMDIL_SRC_XYZ0,
  AMDIL_SRC_0XYZ,
  AMDIL_SRC_XZXZ,
  AMDIL_SRC_YWYW,
  AMDIL_SRC_X0Y0,
  AMDIL_SRC_0X0Y,
  AMDIL_SRC_0YZW,
  AMDIL_SRC_X0ZW,
  AMDIL_SRC_XY0W,
  AMDIL_SRC_XXXX,
  AMDIL_SRC_YYYY,
  AMDIL_SRC_ZZZZ,
  AMDIL_SRC_WWWW,
  AMDIL_SRC_XYXY,
  AMDIL_SRC_ZWZW,
  AMDIL_SRC_YZW0,
  AMDIL_SRC_Z0W0,
  AMDIL_SRC_0Z0W,
  AMDIL_SRC_LAST
};
enum DstSwizzles {
  AMDIL_DST_DFLT = 0,
  AMDIL_DST_X___,
  AMDIL_DST__Y__,
  AMDIL_DST___Z_,
  AMDIL_DST____W,
  AMDIL_DST_XY__,
  AMDIL_DST___ZW,
  AMDIL_DST_XYZ_,
  AMDIL_DST_LAST
};
// Function to get the correct src swizzle string from ID
const char *getSrcSwizzle(unsigned);

// Function to get the correct dst swizzle string from ID
const char *getDstSwizzle(unsigned);

const llvm::TargetRegisterClass *getRegClassFromID(unsigned int ID);

// Utility functions from Type.
const llvm::TargetRegisterClass *getRegClassFromType(unsigned int type);
unsigned int getTargetIndependentMoveFromType(unsigned int type);
unsigned getRegClassFromName(const StringRef &name);

// Debug functions for SDNode and SDValue.
void printSDValue(const llvm::SDValue &Op, int level);
void printSDNode(const llvm::SDNode *N);

// Functions to check if an opcode is a specific type.
bool isMoveOrEquivalent(unsigned int opcode);

// Function to check address space
bool check_type(const llvm::Value *ptr, unsigned int addrspace);

// Group of functions that recursively calculate the number of elements of a
// structure based on it's sub-types.
size_t getNumElements(llvm::Type * const T);
size_t getNumElements(llvm::StructType * const ST);
size_t getNumElements(llvm::IntegerType * const IT);
size_t getNumElements(llvm::FunctionType * const FT);
size_t getNumElements(llvm::ArrayType * const AT);
size_t getNumElements(llvm::VectorType * const VT);
size_t getNumElements(llvm::PointerType * const PT);
size_t getNumElements(llvm::OpaqueType * const OT);
const llvm::Value *getBasePointerValue(const llvm::Value *V);
const llvm::Value *getBasePointerValue(const llvm::MachineInstr *MI);

const char *
getTypeName(llvm::Type *ptr,
            const char *symTab,
            llvm::AMDILMachineFunctionInfo *mMFI,
            bool signedType);

int64_t GET_SCALAR_SIZE(llvm::Type* A);

// Helper functions that check the opcode for status information
bool isLoadInst(const llvm::MachineInstr *MI);
bool isPtrLoadInst(const llvm::MachineInstr *MI);
bool isExtLoadInst(const llvm::MachineInstr *MI);
bool isSWSExtLoadInst(const llvm::MachineInstr *MI);
bool isSExtLoadInst(const llvm::MachineInstr *MI);
bool isZExtLoadInst(const llvm::MachineInstr *MI);
bool isAExtLoadInst(const llvm::MachineInstr *MI);
bool isStoreInst(const llvm::MachineInstr *MI);
bool isPtrStoreInst(const llvm::MachineInstr *MI);
bool isTruncStoreInst(const llvm::MachineInstr *MI);
bool isAtomicInst(const llvm::MachineInstr *MI);
bool isVolatileInst(const llvm::MachineInstr *MI);
bool isGlobalInst(const llvm::MachineInstr *MI);
bool isPrivateInst(const llvm::MachineInstr *MI);
bool isConstantInst(const llvm::MachineInstr *MI);
bool isConstantPoolInst(const llvm::MachineInstr *MI);
bool isRegionInst(const llvm::MachineInstr *MI);
bool isGWSInst(const llvm::MachineInstr *MI);
bool isLocalInst(const llvm::MachineInstr *MI);
bool isImageInst(const llvm::MachineInstr *MI);
bool is64BitImageInst(const llvm::MachineInstr *MI);
bool isWriteImageInst(const llvm::MachineInstr *MI);
bool isReadImageInst(const llvm::MachineInstr *MI);
bool isImageInfoInst(const llvm::MachineInstr *MI);
bool isImageInfo0Inst(const llvm::MachineInstr *MI);
bool isImageInfo1Inst(const llvm::MachineInstr *MI);
bool isImageTXLDInst(const llvm::MachineInstr *MI);
bool isAppendInst(const llvm::MachineInstr *MI);
bool isSemaphoreInst(const llvm::MachineInstr *MI);
bool isRegionAtomic(const llvm::MachineInstr *MI);
bool is64BitRegionAtomic(const llvm::MachineInstr *MI);
bool isLocalAtomic(const llvm::MachineInstr *MI);
bool is64BitLocalAtomic(const llvm::MachineInstr *MI);
bool isGlobalAtomic(const llvm::MachineInstr *MI);
bool is64BitGlobalAtomic(const llvm::MachineInstr *MI);
bool isArenaAtomic(const llvm::MachineInstr *MI);
bool isArenaInst(const llvm::MachineInstr *MI);
bool is64bitLSOp(const llvm::MachineInstr *MI);
bool isLDSInst(const llvm::MachineInstr *MI);
bool isGDSInst(const llvm::MachineInstr *MI);
bool isUAVArenaInst(const llvm::MachineInstr *MI);
bool isUAVRawInst(const llvm::MachineInstr *MI);
bool isCBInst(const llvm::MachineInstr *MI);
bool isScratchInst(const llvm::MachineInstr *MI);
bool is64BitInst(const llvm::MachineInstr *MI);
bool isPackedInst(const llvm::MachineInstr *MI);
bool isSub32BitIOInst(const llvm::MachineInstr *MI);
bool isPackV2I8Inst(const llvm::MachineInstr *MI);
bool isPackV2I16Inst(const llvm::MachineInstr *MI);
bool isPackV4I8Inst(const llvm::MachineInstr *MI);
bool isPackV4I16Inst(const llvm::MachineInstr *MI);
bool isUnpackV2I8Inst(const llvm::MachineInstr *MI);
bool isUnpackV2I16Inst(const llvm::MachineInstr *MI);
bool isUnpackV4I8Inst(const llvm::MachineInstr *MI);
bool isUnpackV4I16Inst(const llvm::MachineInstr *MI);
bool isVectorOpInst(const llvm::MachineInstr *MI);
inline bool isAddriInst(const llvm::MachineInstr *MI) {
  return MI->getOpcode() == llvm::AMDIL::ADDrp
         || MI->getOpcode() == llvm::AMDIL::ADDi64rp;
}
inline bool isAddInst(const llvm::MachineInstr *MI) {
  unsigned opcode = MI->getOpcode();
  return opcode >= llvm::AMDIL::ADDf32rr && opcode <= llvm::AMDIL::ADDv4i8rr;
}
inline bool isCustomAddInst(const llvm::MachineInstr *MI) {
  unsigned opcode = MI->getOpcode();
  return opcode >= llvm::AMDIL::CUSTOM_ADDi16rr
         && opcode <= llvm::AMDIL::CUSTOM_ADDv4i8rr;
}
bool isSkippedLiteral(const llvm::MachineInstr *MI, uint32_t op);
bool isBypassedLiteral(const llvm::MachineInstr *MI, uint32_t op);
// Helper functions that check a registe for status information.
bool isXComponentReg(unsigned);
bool isYComponentReg(unsigned);
bool isZComponentReg(unsigned);
bool isWComponentReg(unsigned);
bool isXYComponentReg(unsigned);
bool isZWComponentReg(unsigned);

// Macros that are used to help with switch statements for various data types
// However, these macro's do not return anything unlike the second set below.
#define ExpandCaseTo32bitIntTypes(Instr) \
case Instr ## i8: \
case Instr ## i16: \
case Instr ## i32:

#define ExpandCaseTo32bitIntTruncTypes(Instr) \
case Instr ## i16i8r: \
case Instr ## i32i8r: \
case Instr ## i32i16r:

#define ExpandCaseToIntTypes(Instr) \
  ExpandCaseTo32bitIntTypes(Instr) \
case Instr ## i64:

#define ExpandCaseToIntTruncTypes(Instr) \
  ExpandCaseTo32bitIntTruncTypes(Instr) \
case Instr ## i64i8r: \
case Instr ## i64i16r: \
case Instr ## i64i32r:

#define ExpandCaseToFloatTypes(Instr) \
case Instr ## f32: \
case Instr ## f64:

#define ExpandCaseToFloatTruncTypes(Instr) \
case Instr ## f64f32r:

#define ExpandCaseTo32bitScalarTypes(Instr) \
  ExpandCaseTo32bitIntTypes(Instr) \
case Instr ## f32:

#define ExpandCaseToAllScalarTypes(Instr) \
  ExpandCaseToFloatTypes(Instr) \
  ExpandCaseToIntTypes(Instr)

#define ExpandCaseToAllScalarTruncTypes(Instr) \
  ExpandCaseToFloatTruncTypes(Instr) \
  ExpandCaseToIntTruncTypes(Instr)

// Vector versions of above macros
#define ExpandCaseToVectorIntTypes(Instr) \
case Instr ## v2i8: \
case Instr ## v4i8: \
case Instr ## v2i16: \
case Instr ## v4i16: \
case Instr ## v2i32: \
case Instr ## v4i32: \
case Instr ## v2i64:

#define ExpandCaseToVectorIntTruncTypes(Instr) \
case Instr ## v2i16i8r: \
case Instr ## v4i16i8r: \
case Instr ## v2i32i8r: \
case Instr ## v4i32i8r: \
case Instr ## v2i32i16r: \
case Instr ## v4i32i16r: \
case Instr ## v2i64i8r: \
case Instr ## v2i64i16r: \
case Instr ## v2i64i32r:

#define ExpandCaseToVectorFloatTypes(Instr) \
case Instr ## v2f32: \
case Instr ## v4f32: \
case Instr ## v2f64:

#define ExpandCaseToVectorFloatTruncTypes(Instr) \
case Instr ## v2f64f32r:

#define ExpandCaseToVectorByteTypes(Instr) \
case Instr ## v4i8: \
case Instr ## v2i16: \
case Instr ## v4i16:

#define ExpandCaseToAllVectorTypes(Instr) \
  ExpandCaseToVectorFloatTypes(Instr) \
  ExpandCaseToVectorIntTypes(Instr)

#define ExpandCaseToAllVectorTruncTypes(Instr) \
  ExpandCaseToVectorFloatTruncTypes(Instr) \
  ExpandCaseToVectorIntTruncTypes(Instr)

#define ExpandCaseToAllTypes(Instr) \
  ExpandCaseToAllVectorTypes(Instr) \
  ExpandCaseToAllScalarTypes(Instr)

#define ExpandCaseToAllTruncTypes(Instr) \
  ExpandCaseToAllVectorTruncTypes(Instr) \
  ExpandCaseToAllScalarTruncTypes(Instr)

#define ExpandCaseToPackedTypes(Instr) \
case Instr ## v2i8: \
case Instr ## v4i8: \
case Instr ## v2i16: \
case Instr ## v4i16:

#define ExpandCaseToByteShortScalarTypes(Instr) \
case Instr ## i8: \
case Instr ## i16:

#define ExpandCaseToByteShortTypes(Instr) \
  ExpandCaseToByteShortScalarTypes(Instr) \
  ExpandCaseToPackedTypes(Instr)

#define ExpandCaseToI8Types(Instr) \
case Instr ## v2i8: \
case Instr ## v4i8:

#define ExpandCaseToI16Types(Instr) \
case Instr ## i16: \
case Instr ## v2i16: \
case Instr ## v4i16:

// Macros that expand into case statements with return values
#define ExpandCaseTo32bitIntReturn(Instr, Return) \
case Instr ## i8: return Return ## i8; \
case Instr ## i16: return Return ## i16; \
case Instr ## i32: return Return ## i32;

#define ExpandCaseToIntReturn(Instr, Return) \
  ExpandCaseTo32bitIntReturn(Instr, Return) \
case Instr ## i64: return Return ## i64;

#define ExpandCaseToFloatReturn(Instr, Return) \
case Instr ## f32: return Return ## f32; \
case Instr ## f64: return Return ## f64;

#define ExpandCaseToAllScalarReturn(Instr, Return) \
  ExpandCaseToFloatReturn(Instr, Return) \
  ExpandCaseToIntReturn(Instr, Return)

// These macros expand to common groupings of RegClass ID's
#define ExpandCaseTo1CompRegID \
case AMDIL::GPRI8RegClassID: \
case AMDIL::GPRI16RegClassID: \
case AMDIL::GPRI32RegClassID: \
case AMDIL::GPRF32RegClassID:

#define ExpandCaseTo2CompRegID \
case AMDIL::GPRI64RegClassID: \
case AMDIL::GPRF64RegClassID: \
case AMDIL::GPRV2I8RegClassID: \
case AMDIL::GPRV2I16RegClassID: \
case AMDIL::GPRV2I32RegClassID: \
case AMDIL::GPRV2F32RegClassID:

// Macros that expand to case statements for specific bitlengths
#define ExpandCaseTo8BitType(Instr) \
case Instr ## i8:

#define ExpandCaseTo16BitType(Instr) \
case Instr ## v2i8: \
case Instr ## i16:

#define ExpandCaseTo32BitType(Instr) \
case Instr ## v4i8: \
case Instr ## v2i16: \
case Instr ## i32: \
case Instr ## f32:

#define ExpandCaseTo64BitType(Instr) \
case Instr ## v4i16: \
case Instr ## v2i32: \
case Instr ## v2f32: \
case Instr ## i64: \
case Instr ## f64:

#define ExpandCaseTo128BitType(Instr) \
case Instr ## v4i32: \
case Instr ## v4f32: \
case Instr ## v2i64: \
case Instr ## v2f64:

bool commaPrint(int i, OSTREAM_TYPE &O);
/// Helper function to get the currently get/set flags.
void getAsmPrinterFlags(llvm::MachineInstr *MI,
                        llvm::AMDILAS::InstrResEnc &curRes);
void setAsmPrinterFlags(llvm::MachineInstr *MI,
                        llvm::AMDILAS::InstrResEnc &curRes);

#endif // AMDILUTILITYFUNCTIONS_H_
