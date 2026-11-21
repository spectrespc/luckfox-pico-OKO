; RAM-resident STM8S block erase stub (see boot_flash.c for the rationale).
; Input  : flash_stub_addr   (0x0200, 16-bit target address)
; Output : flash_stub_result (0x0202: 0 ok, 1 write protected, 2 timeout)
; The image is copied to RAM at boot; keep this file in sync with
; ERASE_STUB_BYTES in boot_flash.c (the .lst gives the opcodes).
        .module erase_stub
        .area CODE_STUB (ABS)
        .org 0

        bres    0x505f, #2          ; IAPSR.EOP = 0 (sticky)
        bset    0x505b, #5          ; CR2 |= ERASE
        bres    0x505c, #5          ; NCR2 &= ~NERASE
        ldw     x, 0x0200           ; flash_stub_addr
        clr     a
        ld      (x), a              ; trigger
        ld      a, #16
        ld      0x0203, a           ; flash_stub_outer
outer$:
        ldw     y, #0xffff
wait$:
        btjt    0x505f, #0, prot$   ; WR_PG_DIS
        btjt    0x505f, #2, ok$     ; EOP
        decw    y
        jrne    wait$
        dec     0x0203
        jrne    outer$
        ld      a, #2
        jra     out$
prot$:
        ld      a, #1
        jra     out$
ok$:
        clr     a
out$:
        ld      0x0202, a           ; flash_stub_result
        bres    0x505b, #5
        bset    0x505c, #5
        ret
