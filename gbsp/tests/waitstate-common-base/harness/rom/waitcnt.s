@ Estimulo de WAITCNT para o teste de savestate.
@ A cada quadro: novo WAITCNT pseudoaleatorio (mascara 0x47FF: SRAM, WS0/WS1/WS2 N e S, prefetch), escrito de um de
@ quatro jeitos (strh, str 32 bits, strb byte a byte, DMA3 de meia palavra), seguido de trabalho que depende das
@ tabelas de waitstate: execucao ARM e Thumb a partir dos espelhos 0x08 (WS0), 0x0A (WS1) e 0x0C (WS2) e cargas
@ 8/16/32 bits de cada regiao. Depois espera o VBlank (idle_wait).
@ r4 soma   r5 PRNG   r6 contador de quadros   r10 EWRAM   r11 IWRAM   r12 IO

.syntax unified
.arm
.global _start

_start:
        mov     r11, #0x03000000
        mov     r10, #0x02000000
        mov     r12, #0x04000000
        mov     r4, #0
        mov     r5, #1                  @ semente (o gerador troca)
        mov     r6, #0
        mov     r1, #0
        strh    r1, [r12]               @ DISPCNT

loop:
        eor     r5, r5, r5, lsl #13
        eor     r5, r5, r5, lsr #17
        eor     r5, r5, r5, lsl #5
        ldr     r1, =0x47FF
        and     r7, r5, r1              @ novo WAITCNT
        add     r2, r12, #0x200         @ 0x04000200
        and     r0, r6, #3
        cmp     r0, #0
        beq     w_strh
        cmp     r0, #1
        beq     w_str
        cmp     r0, #2
        beq     w_strb
        @ DMA3: uma meia palavra de IWRAM para WAITCNT
        strh    r7, [r11, #0x40]
        add     r3, r12, #0xD4
        add     r0, r11, #0x40
        str     r0, [r3]                @ SAD
        add     r0, r2, #4
        str     r0, [r3, #4]            @ DAD = 0x04000204
        ldr     r0, =0x80000001         @ enable, 16 bits, 1 unidade, imediata
        str     r0, [r3, #8]
        b       w_done
w_strh: strh    r7, [r2, #4]
        b       w_done
w_str:  str     r7, [r2, #4]            @ WAITCNT + 0x206 (sem efeito util)
        b       w_done
w_strb: strb    r7, [r2, #4]
        mov     r0, r7, lsr #8
        strb    r0, [r2, #5]
w_done:
        add     r6, r6, #1
        ldrh    r0, [r2, #4]            @ le de volta
        add     r4, r4, r0

        @ trabalho ARM a partir de 0x08 (WS0)
        mov     lr, pc                  @ (bl gera relocacao no clang; b local nao)
        b       work_arm
        @ o mesmo codigo ARM executado pelo espelho 0x0C (WS2)
        ldr     r0, =work_arm
        add     r0, r0, #0x04000000
        mov     lr, pc
        bx      r0
        @ Thumb executado pelo espelho 0x0A (WS1), volta por bx lr
        ldr     r0, =work_thumb
        add     r0, r0, #0x02000000
        orr     r0, r0, #1              @ Thumb
        mov     lr, pc
        bx      r0

        @ fim do quadro: espera o VBlank atual acabar e gira em idle_wait ate o proximo
wait_end:
        ldrh    r0, [r12, #4]
        tst     r0, #1
        bne     wait_end
idle_wait:
        ldrh    r0, [r12, #4]
        tst     r0, #1
        beq     idle_wait
        b       loop
.ltorg

@ trabalho ARM: cargas 8/16/32 de cada espelho do cartucho e um laco curto
work_arm:
        mov     r8, #24
        ldr     r9, =table
wa1:    ldr     r0, [r9]
        ldrh    r1, [r9, #2]
        ldrb    r3, [r9, #1]
        add     r4, r4, r0
        add     r4, r4, r1
        eor     r4, r4, r3
        add     r0, r9, #0x02000000     @ espelho WS1
        ldr     r1, [r0]
        add     r0, r9, #0x04000000     @ espelho WS2
        ldrh    r3, [r0, #4]
        add     r4, r4, r1, ror #3
        add     r4, r4, r3
        ldmia   r9, {r0, r1}
        add     r4, r4, r0
        subs    r8, r8, #1
        bne     wa1
        bx      lr
.ltorg

.thumb
.align 2
work_thumb:
        push    {r4, lr}
        movs    r3, #20
        ldr     r2, =table
wt1:    ldr     r0, [r2]
        ldrh    r1, [r2, #2]
        adds    r0, r0, r1
        ldrb    r1, [r2, #3]
        adds    r0, r0, r1
        subs    r3, r3, #1
        bne     wt1
        pop     {r4}
        adds    r4, r4, r0
        pop     {r0}
        bx      r0
.align 2
.ltorg

.align 2
table:
        .word   0x12345678, 0x9ABCDEF0, 0x0F1E2D3C, 0x4B5A6978
