//===-- AMDILModuleInfo.cpp -----------------------------------------------===//
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

#include "AMDILModuleInfo.h"
#include "AMDILAlgorithms.tpp"
#include "AMDILDevices.h"
#include "AMDILKernel.h"
#include "AMDILMachineFunctionInfo.h"
#include "AMDILSubtarget.h"
#include "AMDILUtilityFunctions.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/Support/FormattedStream.h"
#include <cstdio>

using namespace llvm;

static inline uint32_t AlignDown_32(uint32_t Value, uint32_t Alignment)
{
  return Value & ~(Alignment - 1);
}

static inline uint32_t AlignUp_32(uint32_t Value, uint32_t Alignment)
{
  return AlignDown_32(Value + Alignment - 1, Alignment);
}

AMDILModuleInfo::AMDILModuleInfo(const MachineModuleInfo &MMI)
  : mMMI(&MMI),
    symTab(NULL),
    mSTM(NULL),
    TM(NULL),
    mOffset(0),
    mReservedBuffs(0),
    mCurrentCPOffset(0),
    mPrintfOffset(0),
    mProcessed(false) { }

AMDILModuleInfo::~AMDILModuleInfo()
{
  for (StringMap<AMDILKernel*>::iterator kb = mKernels.begin(), ke = mKernels.end();
       kb != ke; ++kb) {
    StringMapEntry<AMDILKernel*> cur = *kb;
    AMDILKernel *ptr = cur.getValue();
    delete ptr;
  }
}

static const AMDILConstPtr *getConstPtr(const AMDILKernel *krnl, const std::string &arg)
{
  if (!krnl) {
    return NULL;
  }
  llvm::SmallVector<AMDILConstPtr, DEFAULT_VEC_SLOTS>::const_iterator begin, end;
  for (begin = krnl->constPtr.begin(), end = krnl->constPtr.end();
       begin != end; ++begin) {
    if (!strcmp(begin->name.data(),arg.c_str())) {
      return &(*begin);
    }
  }
  return NULL;
}

void AMDILModuleInfo::processModule(const Module *M,
                                    const AMDILTargetMachine *mTM)
{
  Module::const_global_iterator GI;
  Module::const_global_iterator GE;
  mSTM = mTM->getSubtargetImpl();
  TM = mTM;
  if (mProcessed) {
    return;
  }

  for (GI = M->global_begin(), GE = M->global_end(); GI != GE; ++GI) {
    const GlobalValue *GV = GI;
    llvm::StringRef GVName = GV->getName();
    const char *name = GVName.data();

    if (!strncmp(name, "sgv", 3)) {
      mKernelArgs[GVName] = parseSGV(GV);
    } else if (!strncmp(name, "fgv", 3)) {
      // we can ignore this since we don't care about the filename
      // string
    } else if ((name[0] == 'l' || name[0] == 'r') && !strncmp(name + 1, "vgv", 3)) {
      // "lvgv" or "rvgv"
      mLocalArgs[GVName] = parseXVGV(GV);
    } else if (!strncmp(name, "llvm.image.annotations", 22)) {
      parseImageAnnotate(GV);
    } else if (!strncmp(name, "llvm.global.annotations", 23)) {
      parseGlobalAnnotate(GV);
    } else if (!strncmp(name, "llvm.constpointer.annotations", 29)) {
      parseConstantPtrAnnotate(GV);
    } else if (!strncmp(name, "llvm.sampler.annotations", 24)) {
      parseSamplerAnnotate(GV);
    } else if (!strncmp(name, "llvm.argtypename.annotations", 28)) {
      parseIgnoredGlobal(GV);
    } else if (!strncmp(name, "llvm.argtypeconst.annotations", 29)) {
      parseIgnoredGlobal(GV);
    } else if (!strncmp(name, "llvm.readonlypointer.annotations", 32)) {
      parseIgnoredGlobal(GV);
    } else if (!strncmp(name, "llvm.signedOrSignedpointee.annotations", 38)) {
      parseIgnoredGlobal(GV);
    } else if (!strncmp(name, "llvm.restrictpointer.annotations", 32)) {
      parseIgnoredGlobal(GV);
    } else if (!strncmp(name, "llvm.volatilepointer.annotations", 32)) {
      parseIgnoredGlobal(GV);
    } else if (strstr(name, "cllocal")) {
      parseAutoArray(GV, false);
    } else if (strstr(name, "clregion")) {
      parseAutoArray(GV, true);
    } else if (!GV->use_empty()
               && mIgnoreStr.find(GVName) == mIgnoreStr.end()) {
      parseConstantPtr(GV);
    }
  }

  allocateGlobalCB();

  safeForEach(M->begin(), M->end(),
              std::bind1st(
                std::mem_fun(&AMDILModuleInfo::checkConstPtrsUseHW),
                this));
  // Make sure we only process the module once even though this function
  // is called everytime a MachineFunctionInfo object is instantiated.
  mProcessed = true;
}

void AMDILModuleInfo::allocateGlobalCB(void)
{
  uint32_t maxCBSize = mSTM->device()->getMaxCBSize();
  uint32_t offset = 0;
  uint32_t curCB = 0;
  uint32_t swoffset = 0;
  for (StringMap<AMDILConstPtr>::iterator cpb = mConstMems.begin(),
       cpe = mConstMems.end(); cpb != cpe; ++cpb) {
    bool constHW = mSTM->device()->usesHardware(AMDILDeviceInfo::ConstantMem);
    AMDILConstPtr &c = cpb->second;
    c.usesHardware = false;
    if (constHW) {
      // If we have a limit on the max CB Size, then we need to make sure that
      // the constant sizes fall within the limits.
      if (c.size <= maxCBSize) {
        offset = AlignUp_32(offset, c.align);
        if (offset + c.size > maxCBSize) {
          offset = 0;
          curCB++;
        }
        if (curCB < mSTM->device()->getMaxNumCBs()) {
          c.cbNum = curCB + CB_BASE_OFFSET;
          c.offset = offset;
          offset += c.size;
          c.usesHardware = true;
          continue;
        }
      }
    }
    swoffset = AlignUp_32(swoffset, c.align);
    c.cbNum = 0;
    c.offset = swoffset;
    swoffset += c.size;
  }
  if (!mConstMems.empty()) {
    mReservedBuffs = curCB + 1;
  }
}

bool AMDILModuleInfo::checkConstPtrsUseHW(llvm::Module::const_iterator *FCI)
{
  Function::const_arg_iterator AI, AE;
  const Function *func = *FCI;
  std::string name = func->getName();
  AMDILKernel *krnl = mKernels[name];
  if (!krnl || !krnl->mKernel) {
    return false;
  }
  if (mSTM->device()->usesHardware(AMDILDeviceInfo::ConstantMem)) {
    for (AI = func->arg_begin(), AE = func->arg_end();
         AI != AE; ++AI) {
      const Argument *Arg = &(*AI);
      const PointerType *P = dyn_cast<PointerType>(Arg->getType());
      if (!P) {
        continue;
      }
      if (P->getAddressSpace() != AMDILAS::CONSTANT_ADDRESS) {
        continue;
      }
      const AMDILConstPtr *ptr = getConstPtr(krnl, Arg->getName());
      if (ptr) {
        continue;
      }
      AMDILConstPtr constAttr;
      constAttr.name = Arg->getName();
      constAttr.size = this->mSTM->device()->getMaxCBSize();
      constAttr.base = Arg;
      constAttr.isArgument = true;
      constAttr.isArray = false;
      constAttr.offset = 0;
      constAttr.align = 16;
      constAttr.usesHardware =
        mSTM->device()->usesHardware(AMDILDeviceInfo::ConstantMem);
      if (constAttr.usesHardware) {
        constAttr.cbNum = krnl->constPtr.size() + 2;
      } else {
        constAttr.cbNum = 0;
      }
      krnl->constPtr.push_back(constAttr);
    }
  }
  // Now lets make sure that only the N largest buffers
  // get allocated in hardware if we have too many buffers
  uint32_t numPtrs = krnl->constPtr.size();
  if (numPtrs > (this->mSTM->device()->getMaxNumCBs() - mReservedBuffs)) {
    // TODO: Change this routine so it sorts
    // AMDILConstPtr instead of pulling the sizes out
    // and then grab the N largest and disable the rest
    llvm::SmallVector<uint32_t, 16> sizes;
    for (uint32_t x = 0; x < numPtrs; ++x) {
      sizes.push_back(krnl->constPtr[x].size);
    }
    std::sort(sizes.begin(), sizes.end());
    uint32_t numToDisable = numPtrs - (mSTM->device()->getMaxNumCBs() -
                                       mReservedBuffs);
    uint32_t safeSize = sizes[numToDisable-1];
    for (uint32_t x = 0; x < numPtrs && numToDisable; ++x) {
      if (krnl->constPtr[x].size <= safeSize) {
        krnl->constPtr[x].usesHardware = false;
        --numToDisable;
      }
    }
  }
  // Renumber all of the valid CB's so that
  // they are linear increase
  uint32_t CBid = 2 + mReservedBuffs;
  for (uint32_t x = 0; x < numPtrs; ++x) {
    if (krnl->constPtr[x].usesHardware) {
      krnl->constPtr[x].cbNum = CBid++;
    }
  }
  for (StringMap<AMDILConstPtr>::iterator cpb = mConstMems.begin(),
       cpe = mConstMems.end(); cpb != cpe; ++cpb) {
    if (cpb->second.usesHardware) {
      krnl->constPtr.push_back(cpb->second);
    }
  }
  for (uint32_t x = 0; x < krnl->constPtr.size(); ++x) {
    AMDILConstPtr &c = krnl->constPtr[x];
    uint32_t cbNum = c.cbNum - CB_BASE_OFFSET;
    if (cbNum < HW_MAX_NUM_CB && c.cbNum >= CB_BASE_OFFSET) {
      if ((c.size + c.offset) > krnl->constSizes[cbNum]) {
        krnl->constSizes[cbNum] = AlignUp_32(c.size + c.offset, 16);
      }
    } else {
      krnl->constPtr[x].usesHardware = false;
    }
  }
  return false;
}

int32_t AMDILModuleInfo::getArrayOffset(const llvm::StringRef &a) const
{
  StringMap<AMDILArrayMem>::const_iterator iter = mArrayMems.find(a);
  if (iter != mArrayMems.end()) {
    return iter->second.offset;
  } else {
    return -1;
  }
}

int32_t AMDILModuleInfo::getConstOffset(const llvm::StringRef &a) const
{
  StringMap<AMDILConstPtr>::const_iterator iter = mConstMems.find(a);
  if (iter != mConstMems.end()) {
    return iter->second.offset;
  } else {
    return -1;
  }
}

bool AMDILModuleInfo::getConstHWBit(const llvm::StringRef &name) const
{
  StringMap<AMDILConstPtr>::const_iterator iter = mConstMems.find(name);
  if (iter != mConstMems.end()) {
    return iter->second.usesHardware;
  } else {
    return false;
  }
}

// As of right now we only care about the required group size
// so we can skip the variable encoding
AMDILKernelAttr AMDILModuleInfo::parseSGV(const GlobalValue *G)
{
  AMDILKernelAttr nArg;
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(G);
  memset(&nArg, 0, sizeof(nArg));
  for (int x = 0; x < 3; ++x) {
    nArg.reqGroupSize[x] = mSTM->getDefaultSize(x);
    nArg.reqRegionSize[x] = mSTM->getDefaultSize(x);
  }
  if (!GV || !GV->hasInitializer()) {
    return nArg;
  }
  const Constant *CV = GV->getInitializer();
  const ConstantDataArray *CA = dyn_cast_or_null<ConstantDataArray>(CV);
  if (!CA || !CA->isString()) {
    return nArg;
  }
  std::string init = CA->getAsString();
  size_t pos = init.find("RWG");
  if (pos != llvm::StringRef::npos) {
    pos += 3;
    std::string LWS = init.substr(pos, init.length() - pos);
    const char *lws = LWS.c_str();
    sscanf(lws, "%u,%u,%u", &(nArg.reqGroupSize[0]),
           &(nArg.reqGroupSize[1]),
           &(nArg.reqGroupSize[2]));
    nArg.mHasRWG = true;
  }
  pos = init.find("RWR");
  if (pos != llvm::StringRef::npos) {
    pos += 3;
    std::string LWS = init.substr(pos, init.length() - pos);
    const char *lws = LWS.c_str();
    sscanf(lws, "%u,%u,%u", &(nArg.reqRegionSize[0]),
           &(nArg.reqRegionSize[1]),
           &(nArg.reqRegionSize[2]));
    nArg.mHasRWR = true;
  }
  return nArg;
}

AMDILLocalArg AMDILModuleInfo::parseXVGV(const GlobalValue *G)
{
  AMDILLocalArg nArg;
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(G);
  nArg.name = "";
  if (!GV || !GV->hasInitializer()) {
    return nArg;
  }
  const ConstantArray *CA =
    dyn_cast_or_null<ConstantArray>(GV->getInitializer());
  if (!CA) {
    return nArg;
  }
  for (size_t x = 0, y = CA->getNumOperands(); x < y; ++x) {
    const Value *local = CA->getOperand(x);
    const ConstantExpr *CE = dyn_cast_or_null<ConstantExpr>(local);
    if (!CE || !CE->getNumOperands()) {
      continue;
    }
    nArg.name = (*(CE->op_begin()))->getName();
    if (mArrayMems.find(nArg.name) != mArrayMems.end()) {
      nArg.local.push_back(&(mArrayMems[nArg.name]));
    }
  }
  return nArg;
}

void AMDILModuleInfo::parseSamplerAnnotate(const GlobalValue *G)
{
  const GlobalVariable *GV = dyn_cast_or_null<GlobalVariable>(G);
  const ConstantArray *CA =
    dyn_cast_or_null<ConstantArray>(GV->getInitializer());
  if (!CA) {
    return;
  }
  uint32_t numOps = CA->getNumOperands();
  for (uint32_t x = 0; x < numOps; ++x) {
    const ConstantExpr *nameField = dyn_cast<ConstantExpr>(CA->getOperand(x));
    const GlobalVariable *nameGV =
      dyn_cast<GlobalVariable>(nameField->getOperand(0));
    const ConstantDataArray *nameArray =
      dyn_cast<ConstantDataArray>(nameGV->getInitializer());
    std::string nameStr = nameArray->getAsString();
    mSamplerSet[GV->getName()].insert(nameStr.substr(0, nameStr.size()-1));
    // Lets add this string to the set of strings we should ignore processing
    mIgnoreStr.insert(nameGV->getName());
    if (mConstMems.find(nameGV->getName())
        != mConstMems.end()) {
      // If we already processesd this string as a constant, lets remove it from
      // the list of known constants.  This way we don't process unneeded data
      // and don't generate code/metadata for strings that are never used.
      mConstMems.erase(mConstMems.find(nameGV->getName()));
    }

  }
}

void AMDILModuleInfo::parseIgnoredGlobal(const GlobalValue *G)
{
  const GlobalVariable *GV = dyn_cast_or_null<GlobalVariable>(G);
  const ConstantArray *CA =
    dyn_cast_or_null<ConstantArray>(GV->getInitializer());
  if (!CA) {
    return;
  }
  uint32_t numOps = CA->getNumOperands();
  for (uint32_t x = 0; x < numOps; ++x) {
    const ConstantExpr *nameField = dyn_cast<ConstantExpr>(CA->getOperand(x));
    const GlobalVariable *nameGV =
      dyn_cast<GlobalVariable>(nameField->getOperand(0));
    // Lets add this string to the set of strings we should ignore processing
    mIgnoreStr.insert(nameGV->getName());
    if (mConstMems.find(nameGV->getName())
        != mConstMems.end()) {
      // If we already processesd this string as a constant, lets remove it from
      // the list of known constants.  This way we don't process unneeded data
      // and don't generate code/metadata for strings that are never used.
      mConstMems.erase(mConstMems.find(nameGV->getName()));
    }
  }
}

std::set<std::string> *
AMDILModuleInfo::getSamplerForKernel(llvm::StringRef &ref)
{
  return (mSamplerSet.find(ref) != mSamplerSet.end()) ? &mSamplerSet[ref] : NULL;
}

void AMDILModuleInfo::parseConstantPtrAnnotate(const GlobalValue *G)
{
  const GlobalVariable *GV = dyn_cast_or_null<GlobalVariable>(G);
  const ConstantArray *CA =
    dyn_cast_or_null<ConstantArray>(GV->getInitializer());
  if (!CA) {
    return;
  }
  uint32_t numOps = CA->getNumOperands();
  for (uint32_t x = 0; x < numOps; ++x) {
    const Value *V = CA->getOperand(x);
    const ConstantStruct *CS = dyn_cast_or_null<ConstantStruct>(V);
    if (!CS) {
      continue;
    }
    assert(CS->getNumOperands() == 2 && "There can only be 2"
           " fields, a name and size");
    const ConstantExpr *nameField = dyn_cast<ConstantExpr>(CS->getOperand(0));
    const ConstantInt *sizeField = dyn_cast<ConstantInt>(CS->getOperand(1));
    assert(nameField && "There must be a constant name field");
    assert(sizeField && "There must be a constant size field");
    const GlobalVariable *nameGV =
      dyn_cast<GlobalVariable>(nameField->getOperand(0));
    const ConstantDataArray *nameArray =
      dyn_cast<ConstantDataArray>(nameGV->getInitializer());
    // Lets add this string to the set of strings we should ignore processing
    mIgnoreStr.insert(nameGV->getName());
    if (mConstMems.find(nameGV->getName())
        != mConstMems.end()) {
      // If we already processesd this string as a constant, lets remove it from
      // the list of known constants.  This way we don't process unneeded data
      // and don't generate code/metadata for strings that are never used.
      mConstMems.erase(mConstMems.find(nameGV->getName()));
    } else {
      mIgnoreStr.insert(CS->getOperand(0)->getName());
    }
    AMDILConstPtr constAttr;
    constAttr.name = nameArray->getAsString();
    constAttr.size = AlignUp_32(sizeField->getZExtValue(), 16);
    constAttr.base = CS;
    constAttr.isArgument = true;
    constAttr.isArray = false;
    constAttr.cbNum = 0;
    constAttr.offset = 0;
    constAttr.align = 16;
    constAttr.usesHardware = (constAttr.size <= mSTM->device()->getMaxCBSize());
    // Now that we have all our constant information,
    // lets update the AMDILKernel
    llvm::StringRef AMDILKernelName = G->getName().data() + 30;
    AMDILKernel *k;
    if (mKernels.find(AMDILKernelName) != mKernels.end()) {
      k = mKernels[AMDILKernelName];
      k->mName = AMDILKernelName;
    } else {
      k = new AMDILKernel(AMDILKernelName, false);
    }
    constAttr.cbNum = k->constPtr.size() + 2;
    k->constPtr.push_back(constAttr);
    mKernels[AMDILKernelName] = k;
  }
}

void AMDILModuleInfo::parseImageAnnotate(const GlobalValue *G)
{
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(G);
  const ConstantArray *CA = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!CA) {
    return;
  }
  if (isa<GlobalValue>(CA)) {
    return;
  }
  uint32_t e = CA->getNumOperands();
  if (!e) {
    return;
  }
  AMDILKernel *k;
  llvm::StringRef name = G->getName().data() + 23;
  if (mKernels.find(name) != mKernels.end()) {
    k = mKernels[name];
    k->mName = name;
  } else {
    k = new AMDILKernel(name, false);
  }

  for (uint32_t i = 0; i != e; ++i) {
    const Value *V = CA->getOperand(i);
    const Constant *C = dyn_cast<Constant>(V);
    const ConstantStruct *CS = dyn_cast<ConstantStruct>(C);
    if (CS && CS->getNumOperands() == 2) {
      if (mConstMems.find(CS->getOperand(0)->getOperand(0)->getName()) !=
          mConstMems.end()) {
        // If we already processesd this string as a constant, lets remove it
        // from the list of known constants.  This way we don't process unneeded
        // data and don't generate code/metadata for strings that are never
        // used.
        mConstMems.erase(
          mConstMems.find(CS->getOperand(0)->getOperand(0)->getName()));
      } else {
        mIgnoreStr.insert(CS->getOperand(0)->getOperand(0)->getName());
      }
      const ConstantInt *CI = dyn_cast<ConstantInt>(CS->getOperand(1));
      uint32_t val = (uint32_t)CI->getZExtValue();
      if (val == 1) {
        k->readOnly.insert(i);
      } else if (val == 2) {
        k->writeOnly.insert(i);
      } else {
        assert(!"Unknown image type value!");
      }
    }
  }
  mKernels[name] = k;
}

void AMDILModuleInfo::parseAutoArray(const GlobalValue *GV, bool isRegion)
{
  const GlobalVariable *G = dyn_cast<GlobalVariable>(GV);
  AMDILArrayMem tmp;
  tmp.isHW = true;
  tmp.offset = 0;
  tmp.align = std::max(G->getAlignment(), 16U);
  if (G == NULL) {
    tmp.vecSize = 0;
  } else {
    // dereference the pointer type because GlobalVariable is always a pointer
    // type, and we want to calculate the size of the memory that the
    // GlobalVariable pointer points to
    PointerType *pTy = dyn_cast<PointerType>(G->getType());
    assert(pTy && "Global Variable not pointer type");
    Type *ty = pTy->getElementType();
    tmp.vecSize = TM->getTargetData()->getTypeAllocSize(ty);
  }
  tmp.isRegion = isRegion;
  mArrayMems[GV->getName()] = tmp;
}

void AMDILModuleInfo::parseConstantPtr(const GlobalValue *GV)
{
  const GlobalVariable *G = dyn_cast<GlobalVariable>(GV);
  AMDILConstPtr constAttr;
  constAttr.name = G->getName();
  if (G == NULL) {
    constAttr.size = 0;
  } else {
    // dereference the pointer type because GlobalVariable is always a pointer
    // type, and we want to calculate the size of the memory that the
    // GlobalVariable pointer points to
    PointerType *pTy = dyn_cast<PointerType>(G->getType());
    assert(pTy && "Global Variable not pointer type");
    Type *ty = pTy->getElementType();
    constAttr.size = TM->getTargetData()->getTypeAllocSize(ty);
  }
  constAttr.base = GV;
  constAttr.isArgument = false;
  constAttr.isArray = true;
  constAttr.offset = 0;
  constAttr.align = std::max(G->getAlignment(), 16U);
  constAttr.cbNum = 0;
  constAttr.usesHardware = false;
  mConstMems[GV->getName()] = constAttr;
}

void AMDILModuleInfo::parseGlobalAnnotate(const GlobalValue *G)
{
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(G);
  if (!GV->hasInitializer()) {
    return;
  }
  const Constant *CT = GV->getInitializer();
  if (!CT || isa<GlobalValue>(CT)) {
    return;
  }
  const ConstantArray *CA = dyn_cast<ConstantArray>(CT);
  if (!CA) {
    return;
  }

  unsigned int nKernels = CA->getNumOperands();
  for (unsigned int i = 0, e = nKernels; i != e; ++i) {
    parseKernelInformation(CA->getOperand(i));
  }
}

AMDILLocalArg* AMDILModuleInfo::parseKernelLRInfo(AMDILKernel *kernel, const Constant *CV)
{
  llvm::StringRef xvgvName = "";  // lvgv or rvgv

  assert(CV);

  if (CV->getNumOperands()) {
    xvgvName = (*(CV->op_begin()))->getName();
  }

  // There can be multiple local or region arrays, so we
  // need to handle each one separately

  AMDILLocalArg *ptr = NULL;
  if (mLocalArgs.find(xvgvName) != mLocalArgs.end()) {
    ptr = &mLocalArgs[xvgvName];

    llvm::SmallVector<AMDILArrayMem *, DEFAULT_VEC_SLOTS>::iterator ib, ie;
    for (ib = ptr->local.begin(), ie = ptr->local.end(); ib != ie; ++ib) {
      AMDILArrayMem *a = *ib;
      uint32_t *curSize;
      if (a->isRegion) {
        curSize = (a->isHW) ? &kernel->curHWRSize : &kernel->curRSize;
      } else {
        curSize = (a->isHW) ? &kernel->curHWSize : &kernel->curSize;
      }
      a->offset = AlignUp_32(*curSize, a->align);
      *curSize = a->offset + a->vecSize;
    }
  }

  return ptr;
}

void AMDILModuleInfo::parseKernelInformation(const Value *V)
{
  if (isa<GlobalValue>(V)) {
    return;
  }
  const ConstantStruct *CS = dyn_cast_or_null<ConstantStruct>(V);
  if (!CS) {
    return;
  }
  uint32_t N = CS->getNumOperands();
  assert((N == 5 || N == 6) && "Expected 5 or 6 operands");

  AMDILKernel *kernel;

  // The first operand is always a pointer to the AMDILKernel.
  const Constant *CV = dyn_cast<Constant>(CS->getOperand(0));
  llvm::StringRef AMDILKernelName = "";
  if (CV->getNumOperands()) {
    AMDILKernelName = (*(CV->op_begin()))->getName();
  }

  // If we have images, then we have already created the AMDILKernel and we just need
  // to get the AMDILKernel information.
  if (mKernels.find(AMDILKernelName) != mKernels.end()) {
    kernel = mKernels[AMDILKernelName];
    kernel->mKernel = true;
    kernel->mName = AMDILKernelName;
  } else {
    kernel = new AMDILKernel(AMDILKernelName, true);
  }

  // The second operand is SGV, there can only be one so we don't need to worry
  // about parsing out multiple data points.
  CV = dyn_cast<Constant>(CS->getOperand(1));

  llvm::StringRef sgvName;
  if (CV->getNumOperands()) {
    sgvName = (*(CV->op_begin()))->getName();
  }

  if (mKernelArgs.find(sgvName) != mKernelArgs.end()) {
    kernel->sgv = &mKernelArgs[sgvName];
  }


  // The third operand is FGV, which is skipped

  // The fourth operand is LVGV
  kernel->lvgv = parseKernelLRInfo(kernel, dyn_cast<Constant>(CS->getOperand(3)));

  // The possibly missing (e.g. on Apple) fifth operand is RVGV
  if (N >= 5) {
    kernel->rvgv = parseKernelLRInfo(kernel, dyn_cast<Constant>(CS->getOperand(4)));
  }

  // The last (fifth or sixth) operand is NULL

  mKernels[AMDILKernelName] = kernel;
}

AMDILKernel *
AMDILModuleInfo::getKernel(const llvm::StringRef &name)
{
  StringMap<AMDILKernel*>::iterator iter = mKernels.find(name);
  if (iter == mKernels.end()) {
    return NULL;
  } else {
    return iter->second;
  }
}

bool AMDILModuleInfo::isKernel(const llvm::StringRef &name) const
{
  return (mKernels.find(name) != mKernels.end());
}

bool AMDILModuleInfo::isWriteOnlyImage(const llvm::StringRef &name,
                                       uint32_t iID) const
{
  const StringMap<AMDILKernel*>::const_iterator kiter = mKernels.find(name);
  if (kiter == mKernels.end()) {
    return false;
  }
  return kiter->second->writeOnly.count(iID);
}

bool AMDILModuleInfo::isReadOnlyImage(const llvm::StringRef &name,
                                      uint32_t iID) const
{
  const StringMap<AMDILKernel*>::const_iterator kiter = mKernels.find(name);
  if (kiter == mKernels.end()) {
    return false;
  }
  return kiter->second->readOnly.count(iID);
}

int32_t AMDILModuleInfo::getArgID(const Argument *arg)
{
  DenseMap<const Argument *, int32_t>::iterator argiter = mArgIDMap.find(arg);
  if (argiter != mArgIDMap.end()) {
    return argiter->second;
  } else {
    return -1;
  }
}

uint32_t
AMDILModuleInfo::getRegion(const llvm::StringRef &name, uint32_t dim) const
{
  StringMap<AMDILKernel*>::const_iterator iter = mKernels.find(name);
  if (iter != mKernels.end() && iter->second->sgv) {
    AMDILKernelAttr *sgv = iter->second->sgv;
    switch (dim) {
    default:
      break;
    case 0:
    case 1:
    case 2:
      return sgv->reqRegionSize[dim];
      break;
    case 3:
      return sgv->reqRegionSize[0] *
             sgv->reqRegionSize[1] *
             sgv->reqRegionSize[2];
    };
  }
  switch (dim) {
  default:
    return 1;
  case 3:
    return mSTM->getDefaultSize(0) *
           mSTM->getDefaultSize(1) *
           mSTM->getDefaultSize(2);
  case 2:
  case 1:
  case 0:
    return mSTM->getDefaultSize(dim);
    break;
  };
  return 1;
}

StringMap<AMDILConstPtr>::iterator AMDILModuleInfo::consts_begin()
{
  return mConstMems.begin();
}


StringMap<AMDILConstPtr>::iterator AMDILModuleInfo::consts_end()
{
  return mConstMems.end();
}

bool AMDILModuleInfo::consts_empty()
{
  return mConstMems.empty();
}

bool AMDILModuleInfo::byteStoreExists(StringRef S) const
{
  return mByteStore.find(S) != mByteStore.end();
}

bool AMDILModuleInfo::usesHWConstant(const AMDILKernel *krnl,
                                     const llvm::StringRef &arg)
{
  const AMDILConstPtr *curConst = getConstPtr(krnl, arg);
  if (!curConst) {
    return false;
  }
  return curConst->usesHardware;
}

uint32_t AMDILModuleInfo::getConstPtrSize(const AMDILKernel *krnl,
    const llvm::StringRef &arg)
{
  const AMDILConstPtr *curConst = getConstPtr(krnl, arg);
  if (!curConst) {
    return 0;
  }
  return curConst->size;
}

uint32_t AMDILModuleInfo::getConstPtrOff(const AMDILKernel *krnl,
    const llvm::StringRef &arg)
{
  const AMDILConstPtr *curConst = getConstPtr(krnl, arg);
  if (!curConst) {
    return 0;
  }
  return curConst->offset;
}

uint32_t AMDILModuleInfo::getConstPtrCB(const AMDILKernel *krnl,
                                        const llvm::StringRef &arg)
{
  const AMDILConstPtr *curConst = getConstPtr(krnl, arg);
  if (!curConst) {
    return 0;
  }
  return curConst->cbNum;
}

void AMDILModuleInfo::calculateCPOffsets(const MachineFunction *MF,
    AMDILKernel *krnl)
{
  const MachineConstantPool *MCP = MF->getConstantPool();
  if (!MCP) {
    return;
  }
  const std::vector<MachineConstantPoolEntry> consts = MCP->getConstants();
  size_t numConsts = consts.size();
  const TargetData *TD = TM->getTargetData();
  for (size_t x = 0; x < numConsts; ++x) {
    const Constant* constVal = consts[x].Val.ConstVal;
    krnl->CPOffsets.push_back(std::make_pair(mCurrentCPOffset, constVal));
    // Align the size to the vector boundary
    uint32_t alignment = 16;
    const GlobalValue *GV = dyn_cast<GlobalValue>(constVal);
    Type* ty = constVal->getType();
    if (GV) {
      alignment = std::max(GV->getAlignment(), 16U);
      // dereference the pointer type because GlobalVariable is always a pointer
      // type, and we want to calculate the size of the memory that the
      // GlobalVariable pointer points to
      PointerType* pTy = dyn_cast<PointerType>(ty);
      assert(pTy && "GlovalVariable not pointer type");
      ty = pTy->getElementType();
    }
    size_t curSize = TD->getTypeAllocSize(ty);
    curSize = AlignUp_32(curSize, alignment);
    mCurrentCPOffset += curSize;
  }
}

bool AMDILModuleInfo::isConstPtrArray(const AMDILKernel *krnl,
                                      const llvm::StringRef &arg)
{
  const AMDILConstPtr *curConst = getConstPtr(krnl, arg);
  if (curConst) {
    return curConst->isArray;
  } else {
    return false;
  }
}

bool AMDILModuleInfo::isConstPtrArgument(const AMDILKernel *krnl,
    const llvm::StringRef &arg)
{
  const AMDILConstPtr *curConst = getConstPtr(krnl, arg);
  if (curConst) {
    return curConst->isArgument;
  } else {
    return false;
  }
}

const Value *AMDILModuleInfo::getConstPtrValue(const AMDILKernel *krnl,
    const llvm::StringRef &arg)
{
  const AMDILConstPtr *curConst = getConstPtr(krnl, arg);
  if (curConst) {
    return curConst->base;
  } else {
    return NULL;
  }
}

static void
dumpZeroElements(StructType * const T, OSTREAM_TYPE &O, bool asBytes);
static void
dumpZeroElements(IntegerType * const T, OSTREAM_TYPE &O, bool asBytes);
static void
dumpZeroElements(ArrayType * const T, OSTREAM_TYPE &O, bool asBytes);
static void
dumpZeroElements(VectorType * const T, OSTREAM_TYPE &O, bool asBytes);
static void
dumpZeroElements(Type * const T, OSTREAM_TYPE &O, bool asBytes);

void dumpZeroElements(Type * const T, OSTREAM_TYPE &O, bool asBytes)
{
  if (!T) {
    return;
  }
  switch(T->getTypeID()) {
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
  case Type::LabelTyID:
    assert(0 && "These types are not supported by this backend");
  default:
  case Type::DoubleTyID:
    if (asBytes) {
      O << ":0:0:0:0:0:0:0:0";
    } else {
      O << ":0";
    }
    break;
  case Type::FloatTyID:
  case Type::PointerTyID:
  case Type::FunctionTyID:
    if (asBytes) {
      O << ":0:0:0:0";
    } else {
      O << ":0";
    }
  case Type::IntegerTyID:
    dumpZeroElements(dyn_cast<IntegerType>(T), O, asBytes);
    break;
  case Type::StructTyID: {
    const StructType *ST = cast<StructType>(T);
    if (!ST->isOpaque()) {
      dumpZeroElements(dyn_cast<StructType>(T), O, asBytes);
    } else { // A pre-LLVM 3.0 opaque type
      if (asBytes) {
        O << ":0:0:0:0";
      } else {
        O << ":0";
      }
    }
  }
  break;
  case Type::ArrayTyID:
    dumpZeroElements(dyn_cast<ArrayType>(T), O, asBytes);
    break;
  case Type::VectorTyID:
    dumpZeroElements(dyn_cast<VectorType>(T), O, asBytes);
    break;
  };
}

void
dumpZeroElements(StructType * const ST, OSTREAM_TYPE &O, bool asBytes)
{
  if (!ST) {
    return;
  }
  Type *curType;
  StructType::element_iterator eib = ST->element_begin();
  StructType::element_iterator eie = ST->element_end();
  for (; eib != eie; ++eib) {
    curType = *eib;
    dumpZeroElements(curType, O, asBytes);
  }
}

void
dumpZeroElements(IntegerType * const IT, OSTREAM_TYPE &O, bool asBytes)
{
  if (asBytes) {
    unsigned byteWidth = (IT->getBitWidth() >> 3);
    for (unsigned x = 0; x < byteWidth; ++x) {
      O << ":0";
    }
  }
}

void
dumpZeroElements(ArrayType * const AT, OSTREAM_TYPE &O, bool asBytes)
{
  size_t size = AT->getNumElements();
  for (size_t x = 0; x < size; ++x) {
    dumpZeroElements(AT->getElementType(), O, asBytes);
  }
}

void
dumpZeroElements(VectorType * const VT, OSTREAM_TYPE &O, bool asBytes)
{
  size_t size = VT->getNumElements();
  for (size_t x = 0; x < size; ++x) {
    dumpZeroElements(VT->getElementType(), O, asBytes);
  }
}

void AMDILModuleInfo::printConstantValue(const Constant *CAval,
    OSTREAM_TYPE &O, bool asBytes)
{
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CAval)) {
    bool isDouble = &CFP->getValueAPF().getSemantics()==&APFloat::IEEEdouble;
    if (isDouble) {
      double val = CFP->getValueAPF().convertToDouble();
      union dtol_union {
        double d;
        uint64_t l;
        char c[8];
      } conv;
      conv.d = val;
      if (!asBytes) {
        O << ":";
        O.write_hex(conv.l);
      } else {
        for (int i = 0; i < 8; ++i) {
          O << ":";
          O.write_hex((unsigned)conv.c[i] & 0xFF);
        }
      }
    } else {
      float val = CFP->getValueAPF().convertToFloat();
      union ftoi_union {
        float f;
        uint32_t u;
        char c[4];
      } conv;
      conv.f = val;
      if (!asBytes) {
        O << ":";
        O.write_hex(conv.u);
      } else {
        for (int i = 0; i < 4; ++i) {
          O << ":";
          O.write_hex((unsigned)conv.c[i] & 0xFF);
        }
      }
    }
  } else if (const ConstantInt *CI = dyn_cast<ConstantInt>(CAval)) {
    uint64_t zVal = CI->getValue().getZExtValue();
    if (!asBytes) {
      O << ":";
      O.write_hex(zVal);
    } else {
      switch (CI->getBitWidth()) {
      default: {
        union ltob_union {
          uint64_t l;
          char c[8];
        } conv;
        conv.l = zVal;
        for (int i = 0; i < 8; ++i) {
          O << ":";
          O.write_hex((unsigned)conv.c[i] & 0xFF);
        }
      }
      break;
      case 8:
        O << ":";
        O.write_hex(zVal & 0xFF);
        break;
      case 16: {
        union stob_union {
          uint16_t s;
          char c[2];
        } conv;
        conv.s = (uint16_t)zVal;
        O << ":";
        O.write_hex((unsigned)conv.c[0] & 0xFF);
        O << ":";
        O.write_hex((unsigned)conv.c[1] & 0xFF);
      }
      break;
      case 32: {
        union itob_union {
          uint32_t i;
          char c[4];
        } conv;
        conv.i = (uint32_t)zVal;
        for (int i = 0; i < 4; ++i) {
          O << ":";
          O.write_hex((unsigned)conv.c[i] & 0xFF);
        }
      }
      break;
      }
    }
  } else if (const ConstantVector *CV = dyn_cast<ConstantVector>(CAval)) {
    int y = CV->getNumOperands()-1;
    int x = 0;
    for (; x < y; ++x) {
      printConstantValue(CV->getOperand(x), O, asBytes);
    }
    printConstantValue(CV->getOperand(x), O, asBytes);
  } else if (const ConstantStruct *CS = dyn_cast<ConstantStruct>(CAval)) {
    int y = CS->getNumOperands();
    int x = 0;
    for (; x < y; ++x) {
      printConstantValue(CS->getOperand(x), O, asBytes);
    }
  } else if (const ConstantAggregateZero *CAZ
             = dyn_cast<ConstantAggregateZero>(CAval)) {
    int y = CAZ->getNumOperands();
    if (y > 0) {
      int x = 0;
      for (; x < y; ++x) {
        printConstantValue((llvm::Constant *)CAZ->getOperand(x),
                           O, asBytes);
      }
    } else {
      if (asBytes) {
        dumpZeroElements(CAval->getType(), O, asBytes);
      } else {
        int y = getNumElements(CAval->getType())-1;
        for (int x = 0; x < y; ++x) {
          O << ":0";
        }
        O << ":0";
      }
    }
  } else if (const ConstantArray *CA = dyn_cast<ConstantArray>(CAval)) {
    int y = CA->getNumOperands();
    int x = 0;
    for (; x < y; ++x) {
      printConstantValue(CA->getOperand(x), O, asBytes);
    }
  } else if (const ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(CAval)) {
    int y = CDS->getNumElements();
    int x = 0;
    for (; x < y; ++x) {
      printConstantValue(CDS->getElementAsConstant(x), O, asBytes);
    }
  } else if (dyn_cast<ConstantPointerNull>(CAval)) {
    O << ":0";
  } else if (dyn_cast<ConstantExpr>(CAval)) {
    O << ":0";
  } else if (dyn_cast<UndefValue>(CAval)) {
    O << ":0";
  } else {
    assert(0 && "Hit condition which was not expected");
  }
}

static bool isStruct(Type * const T)
{
  if (!T) {
    return false;
  }
  switch (T->getTypeID()) {
  default:
    return false;
  case Type::PointerTyID:
    return isStruct(T->getContainedType(0));
  case Type::StructTyID:
    return true;
  case Type::ArrayTyID:
  case Type::VectorTyID:
    return isStruct(dyn_cast<SequentialType>(T)->getElementType());
  };

}

void AMDILModuleInfo::dumpDataToCB(OSTREAM_TYPE &O, AMDILMachineFunctionInfo *mfi,
                                   uint32_t id)
{
  uint32_t size = 0;
  for (StringMap<AMDILConstPtr>::iterator cmb = consts_begin(),
       cme = consts_end(); cmb != cme; ++cmb) {
    AMDILConstPtr& c = cmb->second;
    if (id == c.cbNum) {
      if ((c.size + c.offset) > size) {
        size = AlignUp_32(c.size + c.offset, 16);
      }
    }
  }
  const TargetData *TD = TM->getTargetData();
  if (id == 0) {
    O << ";#DATASTART:" << (size + mCurrentCPOffset) << "\n";
    if (mCurrentCPOffset) {
      for (StringMap<AMDILKernel*>::iterator kcpb = mKernels.begin(),
           kcpe = mKernels.end(); kcpb != kcpe; ++kcpb) {
        const AMDILKernel *k = kcpb->second;
        if (!k) continue;
        size_t numConsts = k->CPOffsets.size();
        for (size_t x = 0; x < numConsts; ++x) {
          size_t offset = k->CPOffsets[x].first;
          const Constant *C = k->CPOffsets[x].second;
          Type *Ty = C->getType();
          if (isa<GlobalVariable>(C)) {
            // dereference the pointer type because GlobalVariable is always
            // a pointer type, and we want to calculate the size of the memory
            // that the GlobalVariable pointer points to
            PointerType* pTy = dyn_cast<PointerType>(Ty);
            assert(pTy && "GlobalVariable not pointer type");
            Ty = pTy->getElementType();
          }
          size_t size = (isStruct(Ty) ? TD->getTypeAllocSize(Ty)
                         : getNumElements(Ty));
          O << ";#" << getTypeName(Ty, symTab, mfi, true) << ":";
          O << offset << ":" << size ;
          printConstantValue(C, O, isStruct(Ty));
          O << "\n";
        }
      }
    }
  } else {
    O << ";#DATASTART:" << id << ":" << size << "\n";
  }

  for (StringMap<AMDILConstPtr>::iterator cmb = consts_begin(), cme = consts_end();
       cmb != cme; ++cmb) {
    if (cmb->second.cbNum != id) {
      continue;
    }
    const GlobalVariable *G = dyn_cast<GlobalVariable>(cmb->second.base);
    Type *Ty = (G) ? G->getType() : NULL;
    size_t offset = cmb->second.offset;
    const Constant *C = G->getInitializer();
    size_t size = (isStruct(Ty)
                   ? cmb->second.size
                   : getNumElements(Ty));
    O << ";#" << getTypeName(Ty, symTab, mfi, true) << ":";
    if (!id) {
      O << (offset + mCurrentCPOffset) << ":" << size;
    } else {
      O << offset << ":" << size;
    }
    if (C) {
      printConstantValue(C, O, isStruct(Ty));
    } else {
      assert(0 && "Cannot have a constant pointer"
             " without an initializer!");
    }
    O <<"\n";
  }
  if (id == 0) {
    O << ";#DATAEND\n";
  } else {
    O << ";#DATAEND:" << id << "\n";
  }
}

void
AMDILModuleInfo::dumpDataSection(OSTREAM_TYPE &O, AMDILMachineFunctionInfo *mfi)
{
  if (consts_empty() && !mCurrentCPOffset) {
    return;
  } else {
    llvm::DenseSet<uint32_t> const_set;
    for (StringMap<AMDILConstPtr>::iterator cmb = consts_begin(), cme = consts_end();
         cmb != cme; ++cmb) {
      const_set.insert(cmb->second.cbNum);
    }
    if (mCurrentCPOffset) {
      const_set.insert(0);
    }
    for (llvm::DenseSet<uint32_t>::iterator setb = const_set.begin(),
         sete = const_set.end(); setb != sete; ++setb) {
      dumpDataToCB(O, mfi, *setb);
    }
  }
}

/// Create a function ID if it is not known or return the known
/// function ID.
uint32_t AMDILModuleInfo::getOrCreateFunctionID(const GlobalValue* func)
{
  if (func->getName().size()) {
    return getOrCreateFunctionID(func->getName());
  }
  uint32_t id;
  if (mFuncPtrNames.find(func) == mFuncPtrNames.end()) {
    id = mFuncPtrNames.size() + RESERVED_FUNCS + mFuncNames.size();
    mFuncPtrNames[func] = id;
  } else {
    id = mFuncPtrNames[func];
  }
  return id;
}

uint32_t AMDILModuleInfo::getOrCreateFunctionID(const std::string &func)
{
  uint32_t id;
  if (mFuncNames.find(func) == mFuncNames.end()) {
    id = mFuncNames.size() + RESERVED_FUNCS + mFuncPtrNames.size();
    mFuncNames[func] = id;
  } else {
    id = mFuncNames[func];
  }
  return id;
}

