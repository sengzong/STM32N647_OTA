/*
******************************************************************************
**
** @file        : STM32N647X0HXQ_ROMxspi2_RAMxspi1_agent.ld
**
** @brief       : Linker script for the RESIDENT OTA AGENT image.
**
**                Copied from Appli/STM32N647X0HXQ_ROMxspi2_RAMxspi1.ld with a
**                single difference: the XIP base is moved from 0x70100400 to
**                0x71400400, i.e. into the agent partition the boot manager
**                owns (BM_AGENT_OFFSET 0x1400000 + BM_AGENT_HEADER 0x400).
**
**                The FSBL does not authenticate this image - bm_jump_to() only
**                checks the vector table (SP in RAM, Reset_Handler inside the
**                XIP window) and then branches, so the linker script simply has
**                to place .isr_vector at the very first byte of the partition.
**
**                Partition size: BM_AGENT_OFFSET(0x1400000) up to the 4 MB the
**                boot manager reserves for it -> 4095 KB after the 0x400 header.
**                The image itself is ~120 KB, so the slack is intentional.
**
**                The internal RAM layout (0x34000000..0x34200000, absolute
**                .tcp_sec at 0x341F8000 and .nx_sec at 0x341F8180) is IDENTICAL
**                to the application's. That is safe by construction: the FSBL
**                either boots the application or boots this agent, never both.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed as is, without any warranty
**                of any kind.
**
******************************************************************************
** @attention
**
** Copyright (c) 2024 STMicroelectronics.
** All rights reserved.
**
** This software is licensed under terms that can be found in the LICENSE file
** in the root directory of this software component.
** If no LICENSE file comes with this software, it is provided AS-IS.
**
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)
_Min_Heap_Size = 0x200; /* required amount of heap */
_Min_Stack_Size = 0x2000; /* required amount of stack */

/* Highest address of the user mode stack */
_estack = 0x341F8000; /* Keep the MSP below Ethernet descriptors and pool. */ /* end of "RAM" Ram type memory */
_sstack = _estack - _Min_Stack_Size;

/* Memories definition */
MEMORY
{
  ROM    (xrw)    : ORIGIN = 0x71400400,   LENGTH = 4095K
  RAM    (xrw)    : ORIGIN = 0x34000100,   LENGTH = 2048K - 256
  EXTRAM (rw)     : ORIGIN = 0x90000000,   LENGTH = 32M
}

/* Sections */
SECTIONS
{
  /* The startup code into "ROM" (XIP in NOR) */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >ROM

  /* External RAM */
  .EXTRAM (NOLOAD):
  {
    . = ALIGN(8);
    *(.EXTRAM)
    . = ALIGN(32);
    *(.OTA_ASSETS)
    . = ALIGN(8);
  } >EXTRAM

  /* The program code and other data into "ROM" Ram type memory */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >ROM

  /* Constant data into "ROM" Ram type memory */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >ROM

  .ARM.extab   (READONLY) :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >ROM

  .ARM   (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >ROM

  .preinit_array   (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >ROM

  .init_array   (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >ROM

  .fini_array   (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >ROM

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections into "RAM" Ram type memory */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */

  } >RAM AT> ROM

  .noncacheable :
  {
    . = ALIGN(8);
    __snoncacheable = .;/* create symbol for start of section */
    KEEP(*(.noncacheable))
    . = ALIGN(8);
    __enoncacheable = .;  /* create symbol for end of section */
  } > RAM

  .gnu.sgstubs :
  {
    . = ALIGN(4);
    *(.gnu.sgstubs*)   /* Secure Gateway stubs */
    . = ALIGN(4);
  } >ROM

  /* Uninitialized data section into "RAM" Ram type memory */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM

  /* User_heap_stack section, used to check that there is enough "RAM" Ram  type memory left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  ASSERT(ADDR(._user_heap_stack) + SIZEOF(._user_heap_stack) <= 0x341B0000,
         "Application data overlaps retained OTA handoff")

  .tcp_sec (NOLOAD) :
  {
    . = ABSOLUTE(0x341F8000);
    *(.RxDecripSection)
    . = ABSOLUTE(0x341F80C0);
    *(.TxDecripSection)
  } >RAM

  .nx_sec (NOLOAD):
  {
    . = ABSOLUTE(0x341F8180);
    *(.NetXPoolSection)

  } >RAM

  /* Remove information from the compiler libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
