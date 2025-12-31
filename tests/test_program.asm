    opt c,cre,l,s

* IC36 on MCU board: Sound and commas
* Connector 1J8, upper right of board (pin 1 on top)
pin1_strobe     equ $40     1J8/1 PB6
pin2_strobe     equ $80     1J8/2 PB7

pia_data_a      equ $2100
pia_ddr_a       equ pia_data_a
pia_ctrl_a      equ pia_data_a+1
pia_data_b      equ pia_data_a+2
pia_ddr_b       equ pia_data_b
pia_ctrl_b      equ pia_data_a+3

    org $f000

main
    sei                     disable interrupts
    lds     #$ff            init stack pointer
    jsr     init_pia
    ldaa    #pin1_strobe
    clrb
loop
    staa    pia_data_b      drive pin high
    ldaa    pia_data_b      read it back
    stab    pia_data_b      and drive back low
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    jmp     loop


init_pia
* Upon initialization, the PIA control registers are all 0s
* so the DDRs are selected.
    clr     pia_ctrl_a      select DDRs just in case
    clr     pia_ctrl_b
    ldaa    #$FF
    staa    pia_ddr_a      set to all outputs
    staa    pia_ddr_b      set to all outputs
    ldaa    #$04
    staa    pia_ctrl_a      select data regs
    staa    pia_ctrl_b      select data regs
    clra
    staa    pia_data_a      drive all low
    staa    pia_data_b      drive all low
    rts

nmi_handler
    rti

swi_handler
    rti

irq_handler
    rti

* Vectors
    org $FFF8
irq_entry   fdb irq_handler
swi_entry   fdb swi_handler
nmi_entry   fdb nmi_handler
rst_entry   fdb main
