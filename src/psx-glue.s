    .set SBUS_DEV8_CTRL,      0x1f80101C

    .section .start, "ax", @progbits
    .set noreorder
    .align 2
    .global main
    .global _start
    .type _start, @function

_start:
    lw    $t2, SBUS_DEV8_CTRL
    lui   $t0, 8
    lui   $t1, 1
_check_dev8:
    bge   $t2, $t0, _store_dev8
    nop
    b     _check_dev8
    add   $t2, $t1
_store_dev8:
    sw    $t2, SBUS_DEV8_CTRL

    la    $t0, __bss_start
    la    $t1, __bss_end

    beq   $t0, $t1, _bss_init_skip
    nop

_bss_init:
    sw    $0, 0($t0)
    addiu $t0, 4
    bne   $t0, $t1, _bss_init
    nop

_bss_init_skip:

    j     main
    nop
