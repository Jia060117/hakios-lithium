# SPDX-License-Identifier: BSD-2-Clause-Patent
# Minimal lithium firmware. All allocatable RAM stays in the stock Image window.
[Defines]
  PLATFORM_NAME = lithium
  PLATFORM_GUID = 21F0A980-E655-4B3D-882B-D5BBD384DC62
  PLATFORM_VERSION = 0.1
  DSC_SPECIFICATION = 0x00010005
  OUTPUT_DIRECTORY = Build/lithiumPkg
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = RELEASE|DEBUG
  SKUID_IDENTIFIER = DEFAULT
  FLASH_DEFINITION = lithiumPkg/lithium.fdf
  USE_PHYSICAL_TIMER = 1
  USE_CUSTOM_DISPLAY_DRIVER = 1
  ENABLE_SECUREBOOT = 0

# Generated from the pinned Silicium common DSC up to [Components].
# This avoids importing other boards' device policy or optional drivers.
!include lithiumPkg/Include/Common.dsc.inc

[BuildOptions]
  *_CLANGPDB_AARCH64_CC_FLAGS = -march=armv8-a+crypto
  *_CLANGPDB_AARCH64_CC_FLAGS = -DLITHIUM_BUILD_TAG=$(LITHIUM_BUILD_TAG)ULL
!ifdef LITHIUM_QEMU
  *_CLANGPDB_AARCH64_CC_FLAGS = -DLITHIUM_QEMU=1
!endif

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000004F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x03
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x024B3000
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x41590000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000
  gArmPlatformTokenSpaceGuid.PcdCoreCount|1
  gArmPlatformTokenSpaceGuid.PcdClusterCount|1
!ifdef LITHIUM_QEMU
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x08000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x080A0000
!else
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x09BC0000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x09C00000
!endif
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Mi MIX"
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesData|128
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesCode|128
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesCode|512
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesData|512
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderCode|16

[LibraryClasses]
  MemoryMapLib|lithiumPkg/Library/MemoryMapLib/MemoryMapLib.inf
  PlatformSecLib|lithiumPkg/Library/PlatformSecLib/PlatformSecLib.inf
  PlatformPeiLib|lithiumPkg/Library/PlatformPeiLib/PlatformPeiLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  SerialPortLib|lithiumPkg/Library/SerialPortLib/SerialPortLib.inf
  HwResetSystemLib|lithiumPkg/Library/ResetSystemLib/ResetSystemLib.inf

[Components]
  SiliciumPkg/Sec/Sec.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
  }
  MdeModulePkg/Core/Dxe/DxeMain.inf
  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf
  lithiumPkg/Drivers/LithiumRamDiskDxe/LithiumRamDiskDxe.inf
  lithiumPkg/Drivers/LithiumConsoleDxe/LithiumConsoleDxe.inf
  lithiumPkg/Drivers/LithiumDisplayDxe/LithiumDisplayDxe.inf
  lithiumPkg/Drivers/LithiumBdsDxe/LithiumBdsDxe.inf
