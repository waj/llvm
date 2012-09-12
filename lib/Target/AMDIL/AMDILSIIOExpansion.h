//===-- AMDILSIIOExpansion.h ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef _AMDIL_SIIO_EXPANSION_H_
#define _AMDIL_SIIO_EXPANSION_H_
#include "AMDILIOExpansion.h"
namespace llvm {
// Class that expands IO instructions for the SI family of devices.
// The Global Load/Store functions need to be overloaded from the EG
// class as an arena is not a valid operation on SI, but are valid
// on the EG/NI devices.
class AMDILSIIOExpansionImpl : public AMDILEGIOExpansionImpl {
public:
  AMDILSIIOExpansionImpl(MachineFunction& mf)
    : AMDILEGIOExpansionImpl(mf) {
  };
  virtual ~AMDILSIIOExpansionImpl() {
  };
protected:
  virtual bool
  isIOInstruction(MachineInstr *MI);
  virtual void
  expandIOInstruction(MachineInstr *MI);
  void
  expandGlobalStore(MachineInstr *MI);
  void
  expandGlobalLoad(MachineInstr *MI);
  virtual bool
  isCacheableOp(MachineInstr* MI);
};   // class AMDILSIIOExpansionImpl

class AMDILSIIOExpansion : public MachineFunctionPass {
public:
  static char ID;
public:
  AMDILSIIOExpansion();
  virtual const char* getPassName() const;
  bool runOnMachineFunction(MachineFunction &MF);
};
} // namespace llvm
#endif // _AMDIL_SIIO_EXPANSION_H_
