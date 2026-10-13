@ Estimulo diferencial para CONFIG_GBSP_CPU_ALERT_PER_INSTRUCTION (A1).
@ Exercita todos os caminhos em que cpu_alert e definido ou em que o interpretador salta direto para
@ arm_loop/thumb_loop: HALT e IRQ produzidos por escrita simples e multipla, DMA com IRQ no fim,
@ PUSH/POP/STM/LDM em ARM e Thumb, LDM^ e MOVS pc (restauracao de SPSR) para Thumb e com IRQ pendente,
@ MSR liberando IRQ pendente, BX nos dois sentidos, SWI ARM/Thumb, pares de instrucoes com alerta so na
@ primeira ou so na segunda, e um laco de espera usado como idle loop pelo harness.
@
@ r4 soma de verificacao   r5 PRNG   r6 contador   r8/r9 operandos
@ r10 EWRAM 0x02000000     r11 IWRAM 0x03000000     r12 IO 0x04000000

.syntax unified
.arm
.global _start

_start:
        b       entry

@ ---- handler de IRQ chamado pelo BIOS (modo IRQ, ARM). O BIOS salva r0-r3, r12, lr.
irq_handler:
        mov     r12, #0x04000000
        add     r12, r12, #0x200
        ldr     r2, [r12]               @ IE | IF << 16
        mov     r3, r2, lsr #16
        strh    r3, [r12, #2]           @ reconhece tudo o que estiver pendente
        mov     r0, #0x03000000
        orr     r0, r0, #0x7F00
        orr     r0, r0, #0xF8           @ 0x03007FF8: flag de IntrWait do BIOS
        ldrh    r1, [r0]
        orr     r1, r1, r3
        strh    r1, [r0]
        bx      lr

entry:
        mov     r11, #0x03000000
        mov     r10, #0x02000000
        mov     r12, #0x04000000
        mov     r4, #0
        mov     r5, #1                  @ semente (o gerador troca)
        ldr     r6, =400
        mov     r0, #0x03000000
        orr     r0, r0, #0x7F00
        orr     r0, r0, #0xFC
        adr     r1, irq_handler
        str     r1, [r0]
        mov     r1, #0x08
        strh    r1, [r12, #4]           @ DISPSTAT: IRQ de VBlank
        add     r2, r12, #0x200
        ldr     r1, =0x0801
        strh    r1, [r2]                @ IE = VBlank | DMA3
        mov     r1, #1
        strh    r1, [r2, #8]            @ IME = 1
        mov     r1, #0
        strh    r1, [r12]               @ DISPCNT

loop:
        eor     r5, r5, r5, lsl #13
        eor     r5, r5, r5, lsr #17
        eor     r5, r5, r5, lsl #5
        mov     r8, r5
        eor     r9, r5, r5, ror #7

        @ T1: HALT por STRB (alerta de write_memory8); acorda no VBlank
t1:
        mov     r0, #0
        strb    r0, [r12, #0x301]
        add     r4, r4, #1

        @ T2: IRQ por escrita de IME com VBlank pendente; a instrucao seguinte nao alerta (alerta so na 1a)
t2:
        add     r2, r12, #0x200
        mov     r1, #0
        strh    r1, [r2, #8]            @ IME = 0
w2:     ldrh    r0, [r2, #2]
        tst     r0, #1
        beq     w2
        mov     r1, #1
        strh    r1, [r2, #8]            @ IME = 1 -> IRQ ja
        str     r4, [r10]               @ sem alerta
        add     r4, r4, r0

        @ T3: escrita sem alerta seguida de HALT (alerta so na 2a)
t3:
        str     r5, [r10, #4]
        strb    r1, [r12, #0x301]       @ r1 = 1: HALTCNT bit7=0 -> halt
        eor     r4, r4, r5

        @ T4: IRQ por STM em IME (alerta de exec_arm_block_mem)
t4:
        mov     r1, #0
        strh    r1, [r2, #8]
w4:     ldrh    r0, [r2, #2]
        tst     r0, #1
        beq     w4
        add     r3, r2, #8
        mov     r1, #1
        stmia   r3, {r1}
        add     r4, r4, r3

        @ T5: STM de palavra em 0x300 (POSTFLG/HALTCNT)
t5:
        add     r3, r12, #0x300
        mov     r1, #0
        stmia   r3, {r1}

        @ T6: DMA3 imediata, 16 palavras IWRAM -> EWRAM, IRQ no fim (alerta na escrita do controle)
t6:
        str     r8, [r11, #0x40]
        str     r9, [r11, #0x44]
        add     r3, r12, #0xD4
        add     r0, r11, #0x40
        str     r0, [r3]                @ SAD
        add     r0, r10, #0x100
        str     r0, [r3, #4]            @ DAD
        ldr     r0, =0xC4000010         @ CNT_H = enable | IRQ | 32 bits ; CNT_L = 16
        str     r0, [r3, #8]
        ldr     r0, [r10, #0x104]
        eor     r4, r4, r0

        @ T7: PUSH/POP e STM/LDM ARM
t7:
        stmfd   sp!, {r0-r3, r8, r9}
        mov     r8, #0
        ldmfd   sp!, {r0-r3, r8, r9}
        stmia   r11, {r4, r5, r8}
        ldmia   r11, {r0, r1, r2}
        eor     r4, r4, r2

        @ T8: SWI ARM (Div)
t8:
        mov     r0, r8
        orr     r1, r9, #1
        swi     #0x060000
        mov     r12, #0x04000000        @ o SWI do BIOS usa r12 como rascunho
        add     r4, r4, r0

        @ T9: MSR liberando IRQ pendente (check_for_interrupts no MSR)
t9:
        msr     cpsr_c, #0x9F           @ System, IRQ mascarada
        add     r2, r12, #0x200
w9:     ldrh    r0, [r2, #2]
        tst     r0, #1
        beq     w9
        msr     cpsr_c, #0x1F           @ libera: IRQ pendente entra ja
        add     r4, r4, #3

        @ T10: LDM^ com PC -> Thumb (restauracao de SPSR com T=1)
t10:
        msr     cpsr_c, #0xD3           @ SVC, IRQ/FIQ mascaradas
        mov     r0, #0x3F               @ SPSR = System, Thumb, IRQ liberada
        msr     spsr_fsxc, r0
        ldr     r1, =thumb_ret
        stmfd   sp!, {r1}
        add     sp, sp, #4              @ sem write-back no LDM^ (o banco do write-back e detalhe do emulador)
        ldmdb   sp, {pc}^
.ltorg

arm_cont:
        mov     r12, #0x04000000
        @ T11: LDM^ com PC para ARM com IRQ pendente (check_for_interrupts na restauracao)
t11:
        msr     cpsr_c, #0xD3
        add     r2, r12, #0x200
w11:    ldrh    r0, [r2, #2]
        tst     r0, #1
        beq     w11
        mov     r0, #0x1F               @ SPSR = System, ARM, IRQ liberada
        msr     spsr_fsxc, r0
        adr     r1, after11
        stmfd   sp!, {r1}
        add     sp, sp, #4              @ sem write-back no LDM^ (o banco do write-back e detalhe do emulador)
        ldmdb   sp, {pc}^
after11:
        mov     r12, #0x04000000
        add     r4, r4, #5

        @ T12: MOVS pc, lr (dados com S, Rd=PC) para Thumb e, depois, para ARM com IRQ pendente
t12:
        msr     cpsr_c, #0xD3
        mov     r0, #0x3F
        msr     spsr_fsxc, r0
        ldr     lr, =thumb_ret2
        movs    pc, lr
.ltorg
arm_cont2:
        mov     r12, #0x04000000
        msr     cpsr_c, #0xD3
        add     r2, r12, #0x200
w12:    ldrh    r0, [r2, #2]
        tst     r0, #1
        beq     w12
        mov     r0, #0x1F
        msr     spsr_fsxc, r0
        adr     lr, after12
        movs    pc, lr
after12:
        mov     r12, #0x04000000
        add     r4, r4, #7

        @ fim da iteracao: espera o VBlank atual acabar e gira em idle_wait ate o proximo (idle loop do harness)
wait_end:
        ldrh    r0, [r12, #4]
        tst     r0, #1
        bne     wait_end
idle_wait:
        ldrh    r0, [r12, #4]
        tst     r0, #1
        beq     idle_wait
        subs    r6, r6, #1
        bne     loop
final:
        b       final
.ltorg

@ ---------------------------------------------------------------- Thumb
.thumb
.align 2
thumb_ret:
        @ T13: PUSH/POP e STMIA/LDMIA Thumb, HALT por STRB Thumb, SWI Thumb, BX Thumb -> ARM
t13:
        push    {r0-r3}
        movs    r0, #0
        pop     {r0-r3}
        mov     r0, r11
        stmia   r0!, {r1, r2}
        mov     r0, r11
        ldmia   r0!, {r1, r2}
        movs    r0, #0
        ldr     r1, =0x04000301
        strb    r0, [r1]                @ HALT em Thumb
        mov     r0, r8
        movs    r1, #3
        swi     #6                      @ Div em Thumb (o retorno para ARM recarrega r12)
        adds    r4, r4, r0
        ldr     r0, =arm_cont
        bx      r0
.align 2
thumb_ret2:
        adds    r4, r4, #1
        ldr     r0, =arm_cont2
        bx      r0
.ltorg
