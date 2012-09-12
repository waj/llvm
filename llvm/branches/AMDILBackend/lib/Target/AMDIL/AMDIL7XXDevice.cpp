//===-- AMDIL7XXDevice.cpp ------------------------------------------------===//
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

#include "AMDIL7XXDevice.h"
#include "AMDIL7XXAsmPrinter.h"
#include "AMDILDevice.h"
#include "AMDILIOExpansion.h"
#include "AMDILPointerManager.h"

using namespace llvm;

AMDIL7XXDevice::AMDIL7XXDevice(AMDILSubtarget *ST) : AMDILDevice(ST)
{
  setCaps();
  std::string name = mSTM->getDeviceName();
  if (name == "rv710") {
    mDeviceFlag = OCL_DEVICE_RV710;
  } else if (name == "rv730") {
    mDeviceFlag = OCL_DEVICE_RV730;
  } else {
    mDeviceFlag = OCL_DEVICE_RV770;
  }
}
AMDIL7XXDevice::~AMDIL7XXDevice()
{
}
void AMDIL7XXDevice::setCaps()
{
  mSWBits.set(AMDILDeviceInfo::LocalMem);
}
size_t AMDIL7XXDevice::getMaxLDSSize() const
{
  if (usesHardware(AMDILDeviceInfo::LocalMem)) {
    return MAX_LDS_SIZE_700;
  }
  return 0;
}
size_t AMDIL7XXDevice::getWavefrontSize() const
{
  return AMDILDevice::HalfWavefrontSize;
}
uint32_t AMDIL7XXDevice::getGeneration() const
{
  return AMDILDeviceInfo::HD4XXX;
}
uint32_t AMDIL7XXDevice::getResourceID(uint32_t DeviceID) const
{
  switch (DeviceID) {
  default:
    assert(0 && "ID type passed in is unknown!");
    break;
  case GLOBAL_ID:
  case CONSTANT_ID:
  case RAW_UAV_ID:
  case ARENA_UAV_ID:
    break;
  case LDS_ID:
    if (usesHardware(AMDILDeviceInfo::LocalMem)) {
      return DEFAULT_LDS_ID;
    }
    break;
  case SCRATCH_ID:
    if (usesHardware(AMDILDeviceInfo::PrivateMem)) {
      return DEFAULT_SCRATCH_ID;
    }
    break;
  case GDS_ID:
    assert(0 && "GDS UAV ID is not supported on this chip");
    if (usesHardware(AMDILDeviceInfo::RegionMem)) {
      return DEFAULT_GDS_ID;
    }
    break;
  };

  return 0;
}
uint32_t AMDIL7XXDevice::getMaxNumUAVs() const
{
  return 1;
}
FunctionPass*
AMDIL7XXDevice::getIOExpansion() const
{
  return new AMDIL7XXIOExpansion();
}
AsmPrinter*
AMDIL7XXDevice::getAsmPrinter(AMDIL_ASM_PRINTER_ARGUMENTS) const
{
  return new AMDIL7XXAsmPrinter(ASM_PRINTER_ARGUMENTS);
}
FunctionPass*
AMDIL7XXDevice::getPointerManager(
  TargetMachine& TM, CodeGenOpt::Level OptLevel) const
{
  if (OptLevel == CodeGenOpt::None) {
    // If we have no optimizations, we need to run the
    // more advanced tracking pass as it is possible to
    // loose information through the stack. The EGPM
    // pass tracks this, but the standard pass does not.
    return new AMDILEGPointerManager();
  } else {
    return new AMDILPointerManager();
  }
}
AMDIL770Device::AMDIL770Device(AMDILSubtarget *ST) : AMDIL7XXDevice(ST)
{
  setCaps();
}
AMDIL770Device::~AMDIL770Device()
{
}
void AMDIL770Device::setCaps()
{
  if (mSTM->isOverride(AMDILDeviceInfo::DoubleOps)) {
    mSWBits.set(AMDILDeviceInfo::FMA);
    mHWBits.set(AMDILDeviceInfo::DoubleOps);
  }
  mSWBits.set(AMDILDeviceInfo::BarrierDetect);
  mHWBits.reset(AMDILDeviceInfo::LongOps);
  mSWBits.set(AMDILDeviceInfo::LongOps);
  mSWBits.set(AMDILDeviceInfo::LocalMem);
}
size_t AMDIL770Device::getWavefrontSize() const
{
  return AMDILDevice::WavefrontSize;
}
AMDIL710Device::AMDIL710Device(AMDILSubtarget *ST) : AMDIL7XXDevice(ST)
{
}
AMDIL710Device::~AMDIL710Device()
{
}
size_t AMDIL710Device::getWavefrontSize() const
{
  return AMDILDevice::QuarterWavefrontSize;
}
