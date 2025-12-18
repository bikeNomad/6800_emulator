# MC6809 CPU Support Implementation Plan

## Overview

Add MC6809 CPU emulation to the existing MC6800 emulator through a separate build configuration (`cmake -DCPU_TYPE=CPU_MC6809`). The MC6809 requires:
- Full instruction set (~150 opcodes vs MC6800's ~72)
- New registers (Y, U, DP) and CCR flags (E, F)
- Complex indexed addressing modes (11+ modes)
- Q clock output (90° phase shift from E clock)
- Pin remapping: VMA→Q clock, /NMI→/FIRQ

## User Requirements
✅ **Build approach**: Separate build only
✅ **Instruction set**: Full MC6809 (~150 opcodes)
✅ **Target**: General Williams pinball support
✅ **Q clock**: Proper 90° phase shift from E
✅ **Pin mapping**: /NMI→/FIRQ, VMA→Q

## Implementation Strategy

### 1. Build System (2 days)

**File: `CMakeLists.txt`**

Add CPU_TYPE selection orthogonal to BOARD_TYPE:
```cmake
# Add after line 25 (BOARD_TYPE)
if(NOT DEFINED CPU_TYPE)
    set(CPU_TYPE "CPU_MC6800")
endif()

# Add CPU-specific source files
if(CPU_TYPE STREQUAL "CPU_MC6809")
    list(APPEND EMULATOR_SOURCES
        src/instructions_6809.c
        src/addressing_6809.c
    )
    pico_generate_pio_header(mc6800_emulator ${CMAKE_CURRENT_LIST_DIR}/src/clock_6809.pio)
else()
    pico_generate_pio_header(mc6800_emulator ${CMAKE_CURRENT_LIST_DIR}/src/clock.pio)
endif()

# Pass to compiler (line ~90)
target_compile_definitions(mc6800_emulator PRIVATE
    CPU_TYPE=${CPU_TYPE}
    # ...
)
```

**Build commands**:
```bash
# MC6809
cmake -DBOARD_TYPE=BOARD_PICO2 -DCPU_TYPE=CPU_MC6809 ..
make
# Output: mc6809_emulator.uf2
```

### 2. CPU State Extension (1 day)

**File: `src/cpu_state.h`**

Extend cpu_state_t with conditional compilation:
```c
#define CPU_MC6800 1
#define CPU_MC6809 2

typedef struct {
    uint16_t pc, x, sp;
    uint8_t a, b;
#if CPU_TYPE == CPU_MC6809
    uint16_t y;       // Index register Y
    uint16_t u;       // User stack pointer
    uint8_t dp;       // Direct page register
#endif
    uint8_t ccr;
    bool halted, running, irq_pending;
#if CPU_TYPE == CPU_MC6809
    bool firq_pending;
#else
    bool nmi_pending;
#endif
    uint64_t instruction_count;
} cpu_state_t;

// MC6809 CCR flags
#if CPU_TYPE == CPU_MC6809
  #define CCR_F 0x40  // FIRQ mask
  #define CCR_E 0x80  // Entire flag
  #define CPU_D ((uint16_t)cpu.a << 8 | cpu.b)  // Combined D register
#endif
```

### 3. Q Clock Generation (3 days)

**File: `src/clock_6809.pio` (new)**

PIO program for E and Q clocks with 90° phase shift:
```pio
.program eclock_6809
public entry_point:
    mov x, ~null
.wrap_target
loop:
    set pins, 0b10 [15]   ; Q=1, E=0 (cycles 0-15)
    set pins, 0b11 [15]   ; Q=1, E=1 (cycles 16-31)
    set pins, 0b01 [15]   ; Q=0, E=1 (cycles 32-47)
    set pins, 0b00 [15]   ; Q=0, E=0 (cycles 48-63)
    jmp x-- loop
.wrap

% c-sdk {
static inline void eclock_program_init(PIO pio, uint sm, uint offset,
                                       uint pin_e, uint pin_q) {
    pio_gpio_init(pio, pin_e);
    pio_gpio_init(pio, pin_q);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_q, 2, true);

    pio_sm_config c = eclock_6809_program_get_default_config(offset);
    sm_config_set_set_pins(&c, pin_q, 2);  // 2-bit output

    // ... (same clock divider calculation as clock.pio)
}
%}
```

**Files: `src/clock.h`, `src/clock.c`**

Add Q clock support:
```c
#if CPU_TYPE == CPU_MC6809
void eclock_wait_q_high(void);
bool eclock_read_q(void);
#endif
```

### 4. Pin Mapping (1 day)

**File: `src/board_config.h`**

Remap pins for MC6809:
```c
#if CPU_TYPE == CPU_MC6800
  #define GPIO_VMA    22  // Valid Memory Address
  #define GPIO_NMI    28  // /NMI input
#elif CPU_TYPE == CPU_MC6809
  #define GPIO_QCLOCK 22  // Q clock output (was VMA)
  #define GPIO_FIRQ   28  // /FIRQ input (was /NMI)
#endif

#define GPIO_ECLOCK 21  // E clock (same for both)
#define GPIO_RW     23  // R/W (same)
#define GPIO_IRQ    27  // /IRQ (same)
#define GPIO_RESET  29  // /RESET (same)
```

### 5. Instruction Decoder (2 days)

**File: `src/instructions.c` (modify)**

Add page dispatch to existing instruction_execute():
```c
void __attribute__((section(".time_critical.instruction_execute")))
instruction_execute(void) {
    uint8_t opcode = memory_read_fast(cpu.pc++);
    cpu.instruction_count++;

#if CPU_TYPE == CPU_MC6809
    // Handle 2-byte opcodes
    if (opcode == 0x10) {
        uint8_t page2 = memory_read_fast(cpu.pc++);
        eclock_consume_cycles(1);
        instruction_execute_page2(page2);
    } else if (opcode == 0x11) {
        uint8_t page3 = memory_read_fast(cpu.pc++);
        eclock_consume_cycles(1);
        instruction_execute_page3(page3);
    } else {
        instruction_execute_page1(opcode);
    }
#else
    // MC6800: Existing switch statement unchanged
    switch (opcode) {
        // ... 198 existing cases ...
    }
#endif

    eclock_sync_instruction();
}
```

**File: `src/instructions_6809.c` (new, ~1500 lines)**

Implement three page functions:
```c
void instruction_execute_page1(uint8_t opcode) {
    switch (opcode) {
        case 0x00: /* NEG direct */ break;
        case 0x12: /* NOP */ break;
        // ... ~120 page 1 opcodes ...
    }
}

void instruction_execute_page2(uint8_t opcode) {
    switch (opcode) {
        case 0x21: /* LBRN */ break;
        case 0x8E: /* LDY immediate */ break;
        // ... ~20 page 2 opcodes ...
    }
}

void instruction_execute_page3(uint8_t opcode) {
    switch (opcode) {
        case 0x83: /* CMPD immediate */ break;
        // ... ~15 page 3 opcodes ...
    }
}
```

### 6. Addressing Modes (5 days)

**File: `src/addressing_6809.c` (new, ~300 lines)**

Implement complex indexed addressing decoder:
```c
uint16_t addressing_indexed(void) {
    uint8_t postbyte = memory_read_fast(cpu.pc++);
    uint16_t *reg_ptr;

    // Decode register (bits 5-6)
    switch ((postbyte >> 5) & 0x03) {
        case 0: reg_ptr = &cpu.x; break;
        case 1: reg_ptr = &cpu.y; break;
        case 2: reg_ptr = &cpu.u; break;
        case 3: reg_ptr = &cpu.sp; break;
    }

    // Handle 15 indexed modes:
    // - 5-bit offset (-16 to +15)
    // - Auto-increment/decrement (,R+ ,R++ ,-R ,--R)
    // - Register offset (A,R B,R D,R)
    // - Constant offset (8-bit, 16-bit)
    // - PC-relative (8-bit, 16-bit)
    // - Indirect modes [,R] [,R++] [n,R]

    // ... (consume appropriate cycles per mode)
    return effective_address;
}

uint16_t addressing_direct(void) {
    uint8_t offset = memory_read_fast(cpu.pc++);
    return ((uint16_t)cpu.dp << 8) | offset;  // DP-relative
}
```

**File: `src/addressing_6809.h` (new)**
```c
uint16_t addressing_indexed(void);
uint16_t addressing_direct(void);
uint16_t addressing_extended_indirect(void);
uint16_t addressing_pcrel_8bit(void);
uint16_t addressing_pcrel_16bit(void);
```

### 7. Interrupt Handling (2 days)

**File: `src/interrupts.c`**

Add FIRQ support for MC6809:
```c
#if CPU_TYPE == CPU_MC6809

void interrupt_service_firq(void) {
    if (!cpu_get_flag(CCR_F)) {
        cpu_set_flag(CCR_E, false);  // Partial save
        cpu_push16(cpu.pc);
        cpu_push(cpu.ccr);
        cpu_set_flag(CCR_I, true);
        cpu_set_flag(CCR_F, true);

        uint16_t vector = (memory_read(VECTOR_FIRQ) << 8) |
                         memory_read(VECTOR_FIRQ + 1);
        cpu.pc = vector;
    }
}

void interrupt_service_irq(void) {
    if (!cpu_get_flag(CCR_I)) {
        cpu_set_flag(CCR_E, true);  // Full save

        // Push all registers
        cpu_push16(cpu.pc);
        cpu_push16(cpu.u);
        cpu_push16(cpu.y);
        cpu_push16(cpu.x);
        cpu_push(cpu.dp);
        cpu_push(cpu.b);
        cpu_push(cpu.a);
        cpu_push(cpu.ccr);

        cpu_set_flag(CCR_I, true);

        uint16_t vector = (memory_read(VECTOR_IRQ) << 8) |
                         memory_read(VECTOR_IRQ + 1);
        cpu.pc = vector;
    }
}

void interrupt_check(void) {
    bool reset = bus_read_reset();
    bool firq = bus_read_firq();
    bool irq = bus_read_irq();

    if (reset) { interrupt_service_reset(); return; }
    if (firq && !cpu_get_flag(CCR_F)) { interrupt_service_firq(); return; }
    if (irq && !cpu_get_flag(CCR_I)) { interrupt_service_irq(); }
}

#endif
```

### 8. Core Instructions Implementation (10 days)

Implement instructions in groups:

**Day 1-2: Inherent/Immediate** (~20 opcodes)
- NOP, DAA, SEX, SWI, SWI2, SWI3
- MUL (8×8→16 multiply)
- TFR, EXG (register transfers)
- PSHS, PSHU, PULS, PULU

**Day 3-4: Branches** (~30 opcodes)
- Short branches: BRA, BEQ, BNE, etc.
- Long branches: LBRA, LBEQ, LBNE, etc.
- BSR, LBSR (subroutines)

**Day 5-6: Load/Store** (~30 opcodes)
- LDA, LDB, LDD, LDX, LDY, LDS, LDU
- STA, STB, STD, STX, STY, STS, STU
- All addressing modes per instruction

**Day 7-8: Arithmetic/Logic** (~30 opcodes)
- ADD, ADC, SUB, SBC, CMP (A, B, D variants)
- AND, OR, EOR, BIT
- NEG, COM, INC, DEC, CLR

**Day 9-10: Complex/Special** (~30 opcodes)
- LEA (load effective address: LEAX, LEAY, LEAS, LEAU)
- Shift/rotate: ASL, ASR, LSL, LSR, ROL, ROR
- CWAI (clear and wait for interrupt)
- SYNC (sync to interrupt)
- Page 2/3 instructions

### 9. Testing Strategy

**Phase 1: Build & Infrastructure** (Week 1)
- [ ] Both CPU_TYPE builds compile
- [ ] Clock generation verified with oscilloscope
- [ ] USB commands read new registers

**Phase 2: Instruction Groups** (Week 2-3)
- [ ] Unit test each instruction group
- [ ] Verify cycle counts match datasheet
- [ ] Test addressing modes exhaustively

**Phase 3: Integration** (Week 4-5)
- [ ] Simple test ROM boots
- [ ] Interrupt handling works
- [ ] Williams pinball ROM testing

**Phase 4: Performance** (Week 6)
- [ ] Profile worst-case timing
- [ ] Optimize hot paths if needed
- [ ] Validate <1.12µs per cycle

## Critical Files

### To Create:
1. **src/instructions_6809.c** - Page 1/2/3 instruction implementations (~1500 lines)
2. **src/addressing_6809.c** - Indexed addressing decoder (~300 lines)
3. **src/addressing_6809.h** - Function declarations (~50 lines)
4. **src/clock_6809.pio** - E+Q clock PIO program (~60 lines)
5. **tests/test_6809.c** - Unit tests (~800 lines)

### To Modify:
1. **CMakeLists.txt** - Add CPU_TYPE selection (+40 lines)
2. **src/board_config.h** - Pin remapping (+20 lines)
3. **src/cpu_state.h** - New registers (+30 lines)
4. **src/clock.h/c** - Q clock functions (+45 lines)
5. **src/instructions.c** - Page dispatcher (+50 lines)
6. **src/interrupts.c** - FIRQ handler (+80 lines)
7. **src/bus.c** - FIRQ pin read (+10 lines)

## Implementation Sequence

**Step 1: Build Infrastructure** (2 days)
1. Modify CMakeLists.txt for CPU_TYPE
2. Add CPU_TYPE defines to board_config.h
3. Create stub files
4. Verify both builds compile

**Step 2: CPU State** (1 day)
1. Extend cpu_state_t with Y/U/DP
2. Add CCR_F and CCR_E flags
3. Update cpu_init()

**Step 3: Q Clock** (3 days)
1. Implement clock_6809.pio
2. Update pin mappings
3. Test phase relationship with scope

**Step 4: Addressing Modes** (5 days)
1. Implement addressing_indexed() with all 15 modes
2. Add direct page addressing
3. Write unit tests
4. Verify cycle counts

**Step 5: Page Dispatcher** (2 days)
1. Modify instruction_execute() for page dispatch
2. Create page function shells
3. Test dispatch overhead

**Step 6: Instructions** (10 days)
- Implement in groups (inherent, branches, load/store, arithmetic, special)

**Step 7: Interrupts** (2 days)
1. Implement FIRQ handler
2. Modify IRQ handler
3. Update interrupt_check()

**Step 8: Testing** (5 days)
1. Unit tests per instruction group
2. Integration testing
3. Performance validation

**Total: ~30 days (6 weeks)**

## Key Design Decisions

1. **Separate builds**: No runtime switching, simpler code
2. **Conditional compilation**: `#if CPU_TYPE == CPU_MC6809` pattern
3. **Time-critical sections**: All page functions in RAM
4. **Addressing helper functions**: Centralize complex logic
5. **Q clock via PIO**: Hardware-timed 90° phase shift

## Performance Targets

- Worst-case instruction: <25µs (20 cycles × 1.12µs)
- Average instruction: 4-8 cycles (4.5-9µs)
- Addressing overhead: <10ns function call
- Memory footprint: +25KB flash, +10 bytes RAM

## Success Criteria

✅ Both MC6800 and MC6809 builds compile
✅ MC6809 boots Williams pinball ROM
✅ All 150 opcodes implemented
✅ Cycle counts match datasheet
✅ Q clock 90° ahead of E clock
✅ FIRQ/IRQ interrupts work correctly
✅ No MC6800 regression
