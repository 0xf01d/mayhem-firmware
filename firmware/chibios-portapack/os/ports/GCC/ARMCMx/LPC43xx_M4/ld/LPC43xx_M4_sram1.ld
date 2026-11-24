/*
    ChibiOS/RT - Copyright (C) 2006-2013 Giovanni Di Sirio
                 Copyright (C) 2014 Jared Boone, ShareBrained Technology

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
 * LPC43xx M4 memory setup, code in Local SRAM Bank 1, data and bss in Bank 2.
 *
 * Every other baseband image is linked with "LPC43xx_M4.ld": code, rodata and
 * data go to Local SRAM Bank 2 (32 KiB on HackRF One, 64 KiB on Praline) and
 * only data/bss/stacks to Bank 1. Bank 2 is also where an external app's M0
 * code is loaded, so that script caps a baseband image at 32 KiB on HackRF One.
 *
 * This script splits the other way: code and rodata live in Local SRAM Bank 1
 * (LD_M4_RAM_LEN bytes, 90112 on HackRF One; M4_RAM_SIZE in "rules.cmake"
 * already stops below the M0/M4 shared memory), while data, bss and the stacks
 * go to Bank 2 (LD_M4_FLASH_LEN, 32752 B on HackRF One). An image whose code
 * needs more than 32 KiB but whose static data is small fits this way, which
 * is what the dPMR446 AMBE+2 vocoder needs.
 *
 * The image is loaded at local_sram_0 and the core is pointed at it with
 * M4MEMMAP, as "main.cpp" starts the bundled hackrf.img from
 * m4_code_hackrf. Bank 2 still belongs to the app's own M0 code, so this
 * layout is only for an image whose app does not need that 32 KiB: check
 * memory::map::m4_code before using it.
 */
__main_stack_size__     = 0x0400;   /* Exceptions/interrupts stack */
__process_stack_size__  = 0x1000;   /* main() stack */

MEMORY
{
    /* Local SRAM Bank 1 @ 0x10000000, shadowed to 0x0 for code execution.
     * M4_RAM_SIZE already excludes the shared memory above it. */
    code (rwx) : org = 0x00000000, len = LD_M4_RAM_LEN

    /* Local SRAM Bank 2 @ 0x10080000, data/bss/stacks only. */
    ram  (rwx) : org = 0x10080000, len = LD_M4_FLASH_LEN
}

__ram_start__           = ORIGIN(ram);
__ram_size__            = LENGTH(ram);
__ram_end__             = __ram_start__ + __ram_size__;

ENTRY(ResetHandler)

SECTIONS
{
    . = 0;
    _text = .;

    startup : ALIGN(16) SUBALIGN(16)
    {
        KEEP(*(vectors))
    } > code

    constructors : ALIGN(4) SUBALIGN(4)
    {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    } > code

    destructors : ALIGN(4) SUBALIGN(4)
    {
        PROVIDE(__fini_array_start = .);
        KEEP(*(.fini_array))
        KEEP(*(SORT(.fini_array.*)))
        PROVIDE(__fini_array_end = .);
    } > code

    .text : ALIGN(16) SUBALIGN(16)
    {
        *(.text.startup.*)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.glue_7t)
        *(.glue_7)
        *(.gcc*)
    } > code

    .textalign : ONLY_IF_RO
    {
        . = ALIGN(8);
    } > code

    /* Vocoder state, sized for an AMBE+2 encoder, does not fit Bank 2's 32 KiB
     * beside the stacks and .data, so it is placed here in Bank 1 after the
     * code. Nothing initialises it: xombe_enc_init zeroes what it needs.
     */
    .vocoder_state (NOLOAD) : ALIGN(8)
    {
        *(.vocoder_state)
    } > code

    /* Unwinding metadata is dead weight in a baseband image: nothing here
     * throws and nothing walks a stack. Dropping it saves ~1 KiB of code space
     * and keeps these images inside Bank 1.
     */
    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
    }

    . = ALIGN(4);
    _etext = .;
    _textdata = _etext;

    /* Stacks, data and bss live in Bank 2, which the loader does not copy: the
     * image's .data is initialised in place, so .data needs a load address in
     * the same region it runs from. Keeping it in "ram" with AT(_textdata)
     * would need the image to carry a copy the loader does not write, so the
     * data section is placed after the code in Bank 2 and initialised by the
     * startup code from there.
     */
    .stacks :
    {
        . = ALIGN(8);
        __main_stack_base__ = .;
        . += __main_stack_size__;
        . = ALIGN(8);
        __main_stack_end__ = .;
        __process_stack_base__ = .;
        __main_thread_stack_base__ = .;
        . += __process_stack_size__;
        . = ALIGN(8);
        __process_stack_end__ = .;
        __main_thread_stack_end__ = .;
    } > ram

    .data ALIGN(4) : AT (_textdata)
    {
        . = ALIGN(4);
        PROVIDE(_data = .);
        *(.data)
        *(.data.*)
        *(.ramtext)
        . = ALIGN(4);
        PROVIDE(_edata = .);
    } > ram

    .bss ALIGN(4) : ALIGN(4)
    {
        . = ALIGN(4);
        PROVIDE(_bss_start = .);
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        PROVIDE(_bss_end = .);
    } > ram
}

PROVIDE(end = .);
_end            = .;

/* ChibiOS references these from chmemcore.c even when nothing allocates: the
 * baseband never uses the core heap, so it is empty.
 */
__heap_base__   = _end;
__heap_end__    = _end;
