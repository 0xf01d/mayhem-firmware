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
 * LPC43xx M4 memory setup for the combined dPMR446 image, which carries both
 * the AMBE+2 decoder and the AMBE+2 encoder.
 *
 * "LPC43xx_M4.ld" (every other image) puts code, rodata, data and bss in Bank 2
 * and only the stacks in Bank 1, capping the image at 32 KiB on HackRF One.
 * "LPC43xx_M4_sram1.ld" (the single-direction vocoder images) turns that
 * around: code and rodata in Bank 1, data/bss/stacks in Bank 2.
 *
 * Neither fits the combined image. It needs 54 KiB of code, 25 KiB of tables,
 * 28 KiB of encoder state and 5 KiB of stacks, 121 KiB together, against
 * 90112 + 32752 = 122864 B of local SRAM. That leaves ~1.7 KiB spare, so each
 * byte has to be in the bank that has room for it:
 *
 *   Bank 1 (90112): .text, then .stacks and .vocoder_state, both NOLOAD
 *   Bank 2 (32752): .rodata (the vocoder's codebooks), then .data and .bss
 *
 * Measured on the HackRF One build: Bank 1 89144 B (98.9 %), Bank 2 31952 B
 * (97.6 %). Two things follow from that, and both are the caller's problem:
 *
 *  - Bank 2 is also where an external app's M0 code is loaded, so this image
 *    only works for an app that does not need that region while it runs (see
 *    memory::map::m4_code). The single-direction vocoder images have the same
 *    constraint.
 *  - Stacks and vocoder state are NOLOAD: the loader writes nothing into them,
 *    which is what lets them sit past the end of the loaded code with no gap in
 *    the flat image and no initialisation in the startup code.
 */
__main_stack_size__     = 0x0400;   /* Exceptions/interrupts stack */
__process_stack_size__  = 0x1000;   /* main() stack */

MEMORY
{
    /* Local SRAM Bank 1 @ 0x10000000, shadowed to 0x0 for code execution.
     * M4_RAM_SIZE already excludes the shared memory above it. */
    code (rwx) : org = 0x00000000, len = LD_M4_RAM_LEN

    /* Local SRAM Bank 2 @ 0x10080000. */
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

    /* Code only: the tables go to Bank 2, which is the half with room for them. */
    .text : ALIGN(16) SUBALIGN(16)
    {
        *(.text.startup.*)
        *(.text)
        *(.text.*)
        *(.glue_7t)
        *(.glue_7)
        *(.gcc*)
    } > code

    .textalign : ONLY_IF_RO
    {
        . = ALIGN(8);
    } > code

    /* Stacks follow the code in Bank 1. NOLOAD: nothing is written here. */
    .stacks (NOLOAD) :
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
    } > code

    /* The receive and transmit halves share one vocoder state buffer (see
     * proc_dpmr446_state.cpp): only one direction runs per boot, so the
     * decoder's 5248 B and the encoder's 28608 B never have to be live
     * together. NOLOAD as well; xombe_enc_init/xombe_dec_init zero what they
     * need.
     */
    .vocoder_state (NOLOAD) : ALIGN(8)
    {
        *(.vocoder_state)
    } > code

    /* Unwinding metadata is dead weight in a baseband image: nothing here
     * throws and nothing walks a stack.
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

    /* Tables start Bank 2, then .data and .bss follow. .data is placed in the
     * region it runs from (the loader copies it from the flat image, which
     * carries it right after the code). */
    .rodata ORIGIN(ram) : ALIGN(4)
    {
        *(.rodata)
        *(.rodata.*)
    } > ram

    .data ALIGN(4) :
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
