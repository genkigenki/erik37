/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A WiFi build's memory map (fm1.toml wifi = true; tools/build.py): app.ld's, with fm1-radio's RAM split (the WiFi
 * library's buffers need 128 KiB of RAM, the pool gives up as much) and the WiFi sections. Keep the two in step.
 *
 * Felucca memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C06000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver); 0x01C06000..0x01C08000 stays for RAM-run debug.
 * RAM  : .data/.bss (128 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 128K
    POOL   (rw)  : ORIGIN = 0x01C28000, LENGTH = 0x4C000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    /* JieLi's BT code and constants run from XIP, as in their SDK (sdk_ld_sfc.c); .volatile_ram_code
     * and the radio trim (*.rf_trim, CONFIG_RF_TRIM_CODE_AT_RAM in their demos) run from RAM: the trim
     * draws ~500 ms of high current, and fetching from the flash meanwhile corrupts what is read
     * (the crashes 0.7 s into every BT start, fm1-ble notes/03-05) */
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
              *(.bt_stack_code .ble_sm_code .hci_controller_code .ble_hci_code .ble_att_code .ble_db_code
                .classic_lmp_code .link_bulk_code .classic_rf_code .lmp_irq_code .ble_gatt_code .ble_ll_code
                .ble_rf_code .bt_rf_code .hci_interface_code .vendor_manager_code .device_manager_code)
              *(.*_const)
              *(.crypto_code)           /* the SDK's byte swaps (htons): named here, or it lands outside the image */
              . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) *(.volatile_ram_code) *(*.rf.text.cache.L2.rf_trim)
                  . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.*_data) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    /* keep the pool's layout as fm1-radio has it: the radio hardware writes somewhere in it (unfound), and moving the
     * stacks / heap / lwIP pools by even 4.6 KiB broke its reception; new buffers go on the heap */
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) *(.*_bss) *(.bd_base)
                      *(.sram)                  /* WiFi: zero-initialised (the SDK's .ram0_bss) */
                      . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) *(.memp_memory_x) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
