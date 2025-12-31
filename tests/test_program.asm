    opt c,cre,l,s

* IC36 on MCU board: Sound and commas
* Connector 1J8, upper right of board (pin 1 on top)
pin1_strobe     equ $40     1J8/1 PB6
pin2_strobe     equ $80     1J8/2 PB7

pia_data_a      equ $2100
pia_ctrl_a      equ pia_data_a+1
pia_data_b      equ pia_data_a+2
pia_ctrl_b      equ pia_data_a+3

    org $f000

main
    sei
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
    ldaa    #$FF
    staa    pia_data_a      set to all outputs
    staa    pia_data_b      set to all outputs
    ldaa    #$04
    staa    pia_ctrl_a      select data regs
    staa    pia_ctrl_b      select data regs
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
