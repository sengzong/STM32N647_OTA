#!/usr/bin/env python3
# -*- coding: ascii -*-
"""Generate Agent/.project and Agent/.cproject for the resident OTA agent.

The agent project is a sibling of Appli/ and FSBL/ and reuses the application
sources through Eclipse linked resources, so there is exactly one copy of
app_netxduo.c / app_ota_flash.h in the tree.

Run from the repository root:
    python .workbuddy/tools/gen_agent_project.py
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AGENT = os.path.join(ROOT, "Agent")

PROJECT_NAME = "LED_2_Agent"

# --- HAL driver sources: identical list to Appli (keeps stm32n6xx_it.c happy,
#     which calls HAL_DCMIPP_IRQHandler, plus the MSP file's LTDC/I2C/DMA2D
#     callbacks that reference HAL macros from these modules).
HAL_SRC = [
    "stm32n6xx_hal",
    "stm32n6xx_hal_cortex",
    "stm32n6xx_hal_dcmipp",
    "stm32n6xx_hal_dma",
    "stm32n6xx_hal_dma2d",
    "stm32n6xx_hal_dma_ex",
    "stm32n6xx_hal_eth",
    "stm32n6xx_hal_eth_ex",
    "stm32n6xx_hal_exti",
    "stm32n6xx_hal_gpio",
    "stm32n6xx_hal_i2c",
    "stm32n6xx_hal_i2c_ex",
    "stm32n6xx_hal_ltdc",
    "stm32n6xx_hal_ltdc_ex",
    "stm32n6xx_hal_pwr",
    "stm32n6xx_hal_pwr_ex",
    "stm32n6xx_hal_rcc",
    "stm32n6xx_hal_rcc_ex",
    "stm32n6xx_hal_rif",
    "stm32n6xx_hal_tim",
    "stm32n6xx_hal_tim_ex",
    "stm32n6xx_hal_uart",
    "stm32n6xx_hal_uart_ex",
    "stm32n6xx_hal_xspi",
    "stm32n6xx_hal_sd",
    "stm32n6xx_hal_sd_ex",
    "stm32n6xx_ll_sdmmc",
    "stm32n6xx_ll_dlyb",
]

# --- application sources reused verbatim; the receiver now includes LCD/LVGL
#     while camera and touch remain application-only:
#       app_netxduo.c          ETH + NetXDuo + the whole OTA/slot/BOOTREC logic
#       nx_stm32_phy_driver.c  YT8512C link state mapping
#       stm32n6xx_hal_msp.c    ETH/UART/XSPI/CAM MSP (shared, harmless extras)
#       stm32n6xx_it.c         interrupt vectors, ThreadX SysTick hook
#       syscalls.c/sysmem.c    newlib retarget
#       system_stm32n6xx_s.c   SystemCoreClock
#       tx_initialize_low_level.S  ThreadX port
#       secure_nsc.c           NSC veneers (same as the application)
SHARED_CORE_SRC = [
    "app_netxduo.c",
    "ota_status.c",
    "ota_screen.c",
    "ota_assets.c",
    "ota_asset_font.c",
    "ota_sd_disk.c",
    "nx_stm32_phy_driver.c",
    "secure_nsc.c",
    "stm32n6xx_hal_msp.c",
    "stm32n6xx_it.c",
    "syscalls.c",
    "sysmem.c",
    "system_stm32n6xx_s.c",
    "tx_initialize_low_level.S",
]

# --- BSP drivers linked as individual .c files.
#     NOTE: linked FOLDERS cannot be used for these, because Eclipse only makes
#     them virtual: gcc resolves -I / quoted includes against the real file
#     system, so the header directories have to be referenced by their real
#     path (../../Appli/Drivers/BSP/...). Linked FILES are fine - CDT writes
#     their real absolute path into the generated makefile.
#
#     Drivers/BSP/SYS is deliberately NOT linked: sys_delay_us and
#     sys_clock_config_debug are only used on the DEBUG/standalone-build path,
#     which the agent never takes (the FSBL owns the clocks).
BSP_FILES = [
    ("SYS", ""),          # placeholder, filtered out below
    ("UART", "uart.c"),
    ("YT8512C", "yt8512c.c"),
    ("RGBLCD", "rgblcd.c"),
]
BSP_FILES = [(d, f) for d, f in BSP_FILES if f]


def link(name, uri, kind="1"):
    return (
        "\t\t<link>\n"
        "\t\t\t<name>%s</name>\n"
        "\t\t\t<type>%s</type>\n"
        "\t\t\t<locationURI>%s</locationURI>\n"
        "\t\t</link>\n" % (name, kind, uri)
    )


def build_project():
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>\n')
    out.append("<projectDescription>\n")
    out.append("\t<name>%s</name>\n" % PROJECT_NAME)
    out.append("\t<comment></comment>\n")
    out.append("\t<projects>\n\t</projects>\n")
    out.append("\t<buildSpec>\n")
    out.append("\t\t<buildCommand>\n\t\t\t<name>org.eclipse.cdt.managedbuilder.core.genmakebuilder</name>\n")
    out.append("\t\t\t<triggers>clean,full,incremental,</triggers>\n\t\t\t<arguments>\n\t\t\t</arguments>\n\t\t</buildCommand>\n")
    out.append("\t\t<buildCommand>\n\t\t\t<name>org.eclipse.cdt.managedbuilder.core.ScannerConfigBuilder</name>\n")
    out.append("\t\t\t<triggers>full,incremental,</triggers>\n\t\t\t<arguments>\n\t\t\t</arguments>\n\t\t</buildCommand>\n")
    out.append("\t</buildSpec>\n")
    out.append("\t<natures>\n")
    for n in [
        "com.st.stm32cube.ide.mcu.MCUProjectNature",
        "org.eclipse.cdt.core.cnature",
        "com.st.stm32cube.ide.mcu.MCUCubeIdeServicesRevAev2ProjectNature",
        "com.st.stm32cube.ide.mcu.MCUCubeProjectNature",
        "com.st.stm32cube.ide.mcu.MCUAdvancedStructureProjectNature",
        "com.st.stm32cube.ide.mcu.MCUSecureProjectNature",
        "com.st.stm32cube.ide.mcu.MCUIsSecureRefProjectNature",
        "com.st.stm32cube.ide.mcu.MCUMultiLinkerEnabledProjectNature",
        "com.st.stm32cube.ide.mcu.MCUBootModeEnabledProjectNature",
        "com.st.stm32cube.ide.mcu.MCUSingleCpuProjectNature",
        "org.eclipse.cdt.managedbuilder.core.managedBuildNature",
        "org.eclipse.cdt.managedbuilder.core.ScannerConfigNature",
    ]:
        out.append("\t\t<nature>%s</nature>\n" % n)
    out.append("\t</natures>\n")
    out.append("\t<linkedResources>\n")

    # whole shared middleware tree (ThreadX + NetXDuo) - IDE convenience only;
    # the include paths use the real ../../Middlewares/... location.
    out.append(link("Middlewares", "PARENT-1-PROJECT_LOC/Middlewares", "2"))
    # the application's headers, linked for browsing. The compiler reaches them
    # through the real include path ../../Appli/Core/Inc (see C_INCLUDES).
    out.append(link("Core/Inc", "PARENT-1-PROJECT_LOC/Appli/Core/Inc", "2"))
    out.append(link("Core/Startup/startup_stm32n647x0hxq.s",
                    "PARENT-1-PROJECT_LOC/Appli/Core/Startup/startup_stm32n647x0hxq.s"))
    # BSP driver sources
    for d, f in BSP_FILES:
        out.append(link("Drivers/BSP/%s/%s" % (d, f),
                        "PARENT-1-PROJECT_LOC/Appli/Drivers/BSP/%s/%s" % (d, f)))
    # reused application sources
    for f in SHARED_CORE_SRC:
        out.append(link("Core/Src/%s" % f, "PARENT-1-PROJECT_LOC/Appli/Core/Src/%s" % f))
    # HAL drivers from the shared Drivers tree
    for m in HAL_SRC:
        out.append(link("Drivers/STM32N6xx_HAL_Driver/%s.c" % m,
                        "PARENT-1-PROJECT_LOC/Drivers/STM32N6xx_HAL_Driver/Src/%s.c" % m))

    out.append("\t</linkedResources>\n")
    out.append("</projectDescription>\n")
    return "".join(out)


C_DEFINES = [
    "USE_HAL_DRIVER",
    "STM32N647xx",
    "TX_SINGLE_MODE_SECURE",
    "TX_INCLUDE_USER_DEFINE_FILE",
    "NX_INCLUDE_USER_DEFINE_FILE",
    "LED2_OTA_AGENT",
    "LV_CONF_INCLUDE_SIMPLE",
]

C_INCLUDES = [
    "../Core/Inc",
    "../../Appli/Core/Inc",
    "../../Secure_nsclib",
    "../../Drivers/STM32N6xx_HAL_Driver/Inc",
    "../../Drivers/CMSIS/Device/ST/STM32N6xx/Include",
    "../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy",
    "../../Drivers/CMSIS/Include",
    "../../Appli/Drivers/BSP/UART",
    "../../Appli/Drivers/BSP/YT8512C",
    "../../Appli/Drivers/BSP/RGBLCD",
    "../../Middlewares/LVGL",
    "../../Middlewares/FATFS",
    "../../Middlewares/THREADX/common/inc",
    "../../Middlewares/THREADX/ports/cortex_m55/gnu/inc",
    "../../Middlewares/NETXDUO/common/inc",
    "../../Middlewares/NETXDUO/addons/dhcp",
    "../../Middlewares/NETXDUO/common/drivers/ethernet",
    "../../Middlewares/NETXDUO/ports/cortex_m55/gnu/inc",
    "../../Middlewares/NETXDUO/tsn/inc",
]

CONFIG_ID = "com.st.stm32cube.ide.mcu.gnu.managedbuild.config.exe.release.4100200001"
FOLDER_ID = CONFIG_ID + "."
CHAIN_ID = "com.st.stm32cube.ide.mcu.gnu.managedbuild.toolchain.exe.release.4100200002"
ERR_PARSERS = ("org.eclipse.cdt.core.GASErrorParser;org.eclipse.cdt.core.GmakeErrorParser;"
               "org.eclipse.cdt.core.GLDErrorParser;org.eclipse.cdt.core.CWDLocator;"
               "org.eclipse.cdt.core.GCCErrorParser")


def build_cproject():
    L = []
    a = L.append
    a('<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n')
    a('<?fileVersion 4.0.0?><cproject storage_type_id="org.eclipse.cdt.core.XmlProjectDescriptionStorage">\n')
    a('\t<storageModule moduleId="org.eclipse.cdt.core.settings">\n')
    a('\t\t<cconfiguration id="%s">\n' % CONFIG_ID)
    a('\t\t\t<storageModule buildSystemId="org.eclipse.cdt.managedbuilder.core.configurationDataProvider" '
      'id="%s" moduleId="org.eclipse.cdt.core.settings" name="Release">\n' % CONFIG_ID)
    a('\t\t\t\t<externalSettings/>\n')
    a('\t\t\t\t<extensions>\n')
    for ext, point in [
        ("org.eclipse.cdt.core.ELF", "org.eclipse.cdt.core.BinaryParser"),
        ("org.eclipse.cdt.core.GASErrorParser", "org.eclipse.cdt.core.ErrorParser"),
        ("org.eclipse.cdt.core.GmakeErrorParser", "org.eclipse.cdt.core.ErrorParser"),
        ("org.eclipse.cdt.core.GLDErrorParser", "org.eclipse.cdt.core.ErrorParser"),
        ("org.eclipse.cdt.core.CWDLocator", "org.eclipse.cdt.core.ErrorParser"),
        ("org.eclipse.cdt.core.GCCErrorParser", "org.eclipse.cdt.core.ErrorParser"),
    ]:
        a('\t\t\t\t\t<extension id="%s" point="%s"/>\n' % (ext, point))
    a('\t\t\t\t</extensions>\n')
    a('\t\t\t</storageModule>\n')
    a('\t\t\t<storageModule moduleId="cdtBuildSystem" version="4.0.0">\n')
    a('\t\t\t\t<configuration artifactExtension="elf" artifactName="${ProjName}" '
      'buildArtefactType="org.eclipse.cdt.build.core.buildArtefactType.exe" '
      'buildProperties="org.eclipse.cdt.build.core.buildArtefactType=org.eclipse.cdt.build.core.buildArtefactType.exe,'
      'org.eclipse.cdt.build.core.buildType=org.eclipse.cdt.build.core.buildType.release" '
      'cleanCommand="rm -rf" description="" errorParsers="%s" id="%s" name="Release" '
      'parent="com.st.stm32cube.ide.mcu.gnu.managedbuild.config.exe.release" '
      'postbuildStep="bash &quot;../postbuild.sh&quot;">\n' % (ERR_PARSERS, CONFIG_ID))
    a('\t\t\t\t\t<folderInfo id="%s" name="/" resourcePath="">\n' % FOLDER_ID)
    a('\t\t\t\t\t\t<toolChain id="%s" name="MCU ARM GCC" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.toolchain.exe.release">\n' % CHAIN_ID)

    def opt(superclass, value, value_type="string", extra=""):
        sid = superclass.rsplit(".", 1)[-1]
        a('\t\t\t\t\t\t\t<option id="%s.4100200003" name="%s" superClass="%s" %svalue="%s" valueType="%s"/>\n'
          % (superclass, sid, superclass, extra, value, value_type))

    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_mcu.4100200004" name="MCU" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_mcu" useByScannerDiscovery="true" '
      'value="STM32N647X0HxQ" valueType="string"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_cpuid.4100200005" name="CPU" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_cpuid" useByScannerDiscovery="false" '
      'value="0" valueType="string"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_coreid.4100200006" name="Core" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_coreid" useByScannerDiscovery="false" '
      'value="0" valueType="string"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.fpu.4100200007" name="Floating-point unit" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.fpu" useByScannerDiscovery="true" '
      'value="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.fpu.value.fpv5-d16" valueType="enumerated"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.floatabi.4100200008" name="Floating-point ABI" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.floatabi" useByScannerDiscovery="true" '
      'value="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.floatabi.value.hard" valueType="enumerated"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_board.4100200009" name="Board" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.target_board" useByScannerDiscovery="false" '
      'value="genericBoard" valueType="string"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.convertbinary.4100200010" '
      'name="Convert to binary file (-O binary)" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.convertbinary" useByScannerDiscovery="false" '
      'value="true" valueType="boolean"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.defaults.4100200011" name="Defaults" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.option.defaults" useByScannerDiscovery="false" '
      'value="com.st.stm32cube.ide.common.services.build.inputs.revA.1.0.6 || Release || false || Executable || '
      'com.st.stm32cube.ide.mcu.gnu.managedbuild.option.toolchain.value.workspace || STM32N647X0HxQ || 0 || 0 || '
      'arm-none-eabi- || ${gnu_tools_for_stm32_compiler_path} || ../Core/Inc | ../../Secure_nsclib | '
      '../../Drivers/STM32N6xx_HAL_Driver/Inc | ../../Drivers/CMSIS/Device/ST/STM32N6xx/Include | '
      '../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy | ../../Drivers/CMSIS/Include ||  ||  || USE_HAL_DRIVER | '
      'STM32N647xx ||  || Drivers | Core/Src | Core/Startup ||  ||  || '
      '${workspace_loc:/${ProjName}/STM32N647X0HXQ_ROMxspi2_RAMxspi1_agent.ld} || true || Secure ||  || '
      'secure_nsclib.o ||  || None ||  ||  || .bin" valueType="string"/>\n')
    a('\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.debug.option.cpuclock.4100200012" name="Cpu clock frequence" '
      'superClass="com.st.stm32cube.ide.mcu.debug.option.cpuclock" useByScannerDiscovery="false" '
      'value="75" valueType="string"/>\n')
    a('\t\t\t\t\t\t\t<targetPlatform archList="all" binaryParser="org.eclipse.cdt.core.ELF" '
      'id="com.st.stm32cube.ide.mcu.gnu.managedbuild.targetplatform.4100200013" isAbstract="false" osList="all" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.targetplatform"/>\n')
    a('\t\t\t\t\t\t\t<builder buildPath="${workspace_loc:/%s}/Release" '
      'id="com.st.stm32cube.ide.mcu.gnu.managedbuild.builder.4100200014" keepEnvironmentInBuildfile="false" '
      'managedBuildOn="true" name="Gnu Make Builder" parallelBuildOn="true" parallelizationNumber="optimal" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.builder"/>\n' % PROJECT_NAME)

    # assembler
    a('\t\t\t\t\t\t\t<tool id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.4100200020" '
      'name="MCU/MPU GCC Assembler" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler">\n')
    a('\t\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.option.debuglevel.4100200021" '
      'name="Debug level" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.option.debuglevel" '
      'value="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.option.debuglevel.value.g0" valueType="enumerated"/>\n')
    a('\t\t\t\t\t\t\t\t<option IS_BUILTIN_EMPTY="false" IS_VALUE_EMPTY="false" '
      'id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.option.definedsymbols.4100200022" '
      'name="Define symbols (-D)" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.option.definedsymbols" '
      'valueType="definedSymbols">\n')
    a('\t\t\t\t\t\t\t\t\t<listOptionValue builtIn="false" value="TX_SINGLE_MODE_SECURE"/>\n')
    a('\t\t\t\t\t\t\t\t</option>\n')
    a('\t\t\t\t\t\t\t\t<inputType id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.input.4100200023" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.assembler.input"/>\n')
    a('\t\t\t\t\t\t\t</tool>\n')

    # c compiler
    a('\t\t\t\t\t\t\t<tool id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.4100200030" '
      'name="MCU/MPU GCC Compiler" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler">\n')
    a('\t\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.debuglevel.4100200031" '
      'name="Debug level" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.debuglevel" '
      'useByScannerDiscovery="false" value="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.debuglevel.value.g0" '
      'valueType="enumerated"/>\n')
    a('\t\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.optimization.level.4100200032" '
      'name="Optimization level" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.optimization.level" '
      'useByScannerDiscovery="false" value="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.optimization.level.value.os" '
      'valueType="enumerated"/>\n')
    a('\t\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.mcmse.4100200033" '
      'name="Secure mode (-mcmse)" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.mcmse" '
      'useByScannerDiscovery="false" value="true" valueType="boolean"/>\n')
    a('\t\t\t\t\t\t\t\t<option IS_BUILTIN_EMPTY="false" IS_VALUE_EMPTY="false" '
      'id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.definedsymbols.4100200034" '
      'name="Define symbols (-D)" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.definedsymbols" '
      'useByScannerDiscovery="false" valueType="definedSymbols">\n')
    for d in C_DEFINES:
        a('\t\t\t\t\t\t\t\t\t<listOptionValue builtIn="false" value="%s"/>\n' % d)
    a('\t\t\t\t\t\t\t\t</option>\n')
    a('\t\t\t\t\t\t\t\t<option IS_BUILTIN_EMPTY="false" IS_VALUE_EMPTY="false" '
      'id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.includepaths.4100200035" '
      'name="Include paths (-I)" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.option.includepaths" '
      'useByScannerDiscovery="false" valueType="includePath">\n')
    for p in C_INCLUDES:
        a('\t\t\t\t\t\t\t\t\t<listOptionValue builtIn="false" value="%s"/>\n' % p)
    a('\t\t\t\t\t\t\t\t</option>\n')
    a('\t\t\t\t\t\t\t\t<inputType id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.input.c.4100200036" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.compiler.input.c"/>\n')
    a('\t\t\t\t\t\t\t</tool>\n')

    # cpp compiler (declared so CDT does not fall back oddly; unused by this project)
    a('\t\t\t\t\t\t\t<tool id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler.4100200040" '
      'name="MCU/MPU G++ Compiler" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler">\n')
    a('\t\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler.option.debuglevel.4100200041" '
      'name="Debug level" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler.option.debuglevel" '
      'useByScannerDiscovery="false" value="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler.option.debuglevel.value.g0" '
      'valueType="enumerated"/>\n')
    a('\t\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler.option.optimization.level.4100200042" '
      'name="Optimization level" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler.option.optimization.level" '
      'useByScannerDiscovery="false" value="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.cpp.compiler.option.optimization.level.value.os" '
      'valueType="enumerated"/>\n')
    a('\t\t\t\t\t\t\t</tool>\n')

    # linker
    a('\t\t\t\t\t\t\t<tool id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker.4100200050" '
      'name="MCU/MPU GCC Linker" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker">\n')
    a('\t\t\t\t\t\t\t\t<option id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker.option.script.4100200051" '
      'name="Linker Script (-T)" superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker.option.script" '
      'value="${workspace_loc:/${ProjName}/STM32N647X0HXQ_ROMxspi2_RAMxspi1_agent.ld}" valueType="string"/>\n')
    a('\t\t\t\t\t\t\t\t<inputType id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker.input.4100200052" '
      'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.c.linker.input">\n')
    a('\t\t\t\t\t\t\t\t\t<additionalInput kind="additionalinputdependency" paths="$(USER_OBJS)"/>\n')
    a('\t\t\t\t\t\t\t\t\t<additionalInput kind="additionalinput" paths="$(LIBS)"/>\n')
    a('\t\t\t\t\t\t\t\t</inputType>\n')
    a('\t\t\t\t\t\t\t</tool>\n')

    for tid, tname, tclass in [
        ("cpp.linker", "MCU/MPU G++ Linker", "cpp.linker"),
        ("archiver", "MCU/MPU GCC Archiver", "archiver"),
        ("size", "MCU Size", "size"),
        ("objdump.listfile", "MCU Output Converter list file", "objdump.listfile"),
        ("objcopy.hex", "MCU Output Converter Hex", "objcopy.hex"),
        ("objcopy.binary", "MCU Output Converter Binary", "objcopy.binary"),
        ("objcopy.verilog", "MCU Output Converter Verilog", "objcopy.verilog"),
        ("objcopy.srec", "MCU Output Converter Motorola S-rec", "objcopy.srec"),
        ("objcopy.symbolsrec", "MCU Output Converter Motorola S-rec with symbols", "objcopy.symbolsrec"),
    ]:
        a('\t\t\t\t\t\t\t<tool id="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.%s.4100200060" name="%s" '
          'superClass="com.st.stm32cube.ide.mcu.gnu.managedbuild.tool.%s"/>\n' % (tid, tname, tclass))

    a('\t\t\t\t\t\t</toolChain>\n')
    a('\t\t\t\t\t</folderInfo>\n')
    a('\t\t\t\t\t<sourceEntries>\n')
    for s in ["Core", "Drivers", "Middlewares"]:
        exclude = ' excluding="ST"' if s == "Middlewares" else ''
        a('\t\t\t\t\t\t<entry%s flags="VALUE_WORKSPACE_PATH|RESOLVED" kind="sourcePath" name="%s"/>\n' % (exclude, s))
    a('\t\t\t\t\t</sourceEntries>\n')
    a('\t\t\t\t</configuration>\n')
    a('\t\t\t</storageModule>\n')
    a('\t\t\t<storageModule moduleId="org.eclipse.cdt.core.externalSettings"/>\n')
    a('\t\t</cconfiguration>\n')
    a('\t</storageModule>\n')
    a('\t<storageModule moduleId="org.eclipse.cdt.core.pathentry"/>\n')
    a('\t<storageModule moduleId="cdtBuildSystem" version="4.0.0">\n')
    a('\t\t<project id="%s.null.4100200070" name="%s"/>\n' % (PROJECT_NAME, PROJECT_NAME))
    a('\t</storageModule>\n')
    a('\t<storageModule moduleId="org.eclipse.cdt.core.LanguageSettingsProviders"/>\n')
    a('\t<storageModule moduleId="scannerConfiguration">\n')
    a('\t\t<autodiscovery enabled="true" problemReportingEnabled="true" selectedProfileId=""/>\n')
    a('\t</storageModule>\n')
    a('\t<storageModule moduleId="org.eclipse.cdt.make.core.buildtargets"/>\n')
    a('</cproject>\n')
    return "".join(L)


def main():
    # Physical folders = the PARENTS of every linked resource. Two names are
    # deliberately absent:
    #   Core/Inc                      -> owned by the link to Appli/Core/Inc
    #   Drivers/BSP/SYS               -> not linked at all (see BSP_FILES)
    # Creating a physical folder where a link of the same name is declared makes
    # Eclipse refuse the link, so the list below must stay in sync with the
    # <linkedResources> block.
    checks = [
        ("Core/Src", os.path.join(AGENT, "Core", "Src")),
        ("Core/Startup", os.path.join(AGENT, "Core", "Startup")),
        ("Drivers/BSP/UART", os.path.join(AGENT, "Drivers", "BSP", "UART")),
        ("Drivers/BSP/YT8512C", os.path.join(AGENT, "Drivers", "BSP", "YT8512C")),
        ("Drivers/STM32N6xx_HAL_Driver", os.path.join(AGENT, "Drivers", "STM32N6xx_HAL_Driver")),
    ]
    for label, path in checks:
        if not os.path.isdir(path):
            os.makedirs(path)
            print("created %s" % label.replace("/", os.sep))

    p = os.path.join(AGENT, ".project")
    with open(p, "w", newline="\n") as f:
        f.write(build_project())
    print("wrote %s" % p)

    p = os.path.join(AGENT, ".cproject")
    with open(p, "w", newline="\n") as f:
        f.write(build_cproject())
    print("wrote %s" % p)


if __name__ == "__main__":
    sys.exit(main())
