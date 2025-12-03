# MC6800 Emulator Test Programs

This directory contains test programs to validate the MC6800 emulator functionality.

## Hardware Requirements

- Raspberry Pi Pico 2 W (RP2350 with WiFi)
- USB connection to host computer
- Serial terminal software (screen, minicom, PuTTY, etc.)

## Building and Flashing

1. Build the firmware:
   ```bash
   cd /Users/ned/src/6800_emulator/build
   rm -rf *
   cmake ..
   make -j4
   ```

2. Flash to Pico 2 W:
   - Hold BOOTSEL button while plugging in USB
   - Copy `mc6800_emulator.uf2` to the RPI-RP2 drive
   - Device will reboot automatically

3. Connect to USB CDC interface:
   ```bash
   screen /dev/tty.usbmodem* 115200
   ```
   (On Linux: `/dev/ttyACM0`, On Windows: use PuTTY with COM port)

## Test Programs

### Test 1: Basic Loads (test1_basic.hex)

**Purpose**: Verify instruction fetch, immediate addressing mode, and branch instruction.

**Machine Code**:
```
E000: 86 42        LDAA #$42     ; Load A with 0x42
E002: C6 24        LDAB #$24     ; Load B with 0x24
E004: CE 12 34     LDX  #$1234   ; Load X with 0x1234
E007: 20 FE        BRA  *        ; Loop forever at this address
```

**Test Procedure**:
1. Connect to USB CDC terminal
2. Type `help` to verify commands are working
3. Configure ROM region:
   ```
   config rom e000 2000
   ```
4. Load the test program:
   ```
   load
   ```
   Then paste the contents of `test1_basic.hex` and type:
   ```
   end
   ```
5. Verify the program was loaded:
   ```
   read e000 9
   ```
   Should show: `8642 C624 CE12 3420 FE`

6. Reset the CPU to initialize PC from reset vector:
   ```
   reset
   ```
7. Check that PC was set correctly:
   ```
   status
   ```
   Should show: `PC: $E000`

8. Run the CPU briefly:
   ```
   run
   ```
   Wait ~100ms, then:
   ```
   halt
   ```

9. Check the results:
   ```
   status
   ```

**Expected Results**:
- A = $42
- B = $24
- X = $1234
- PC = $E007 or $E009 (executing the BRA loop)
- No flags should indicate errors

**What This Tests**:
- ✅ Intel HEX loading
- ✅ Reset vector processing (reads from $FFFE)
- ✅ LDAA immediate mode (#$42)
- ✅ LDAB immediate mode (#$24)
- ✅ LDX immediate mode (#$1234)
- ✅ BRA instruction (branch always)
- ✅ Instruction fetch and decode

---

### Test 2: Arithmetic & Memory (test2_arithmetic.hex)

**Purpose**: Verify arithmetic operations, condition code flags, and memory writes.

**Machine Code**:
```
E000: 86 10        LDAA #$10     ; A = 0x10
E002: C6 20        LDAB #$20     ; B = 0x20
E004: 1B           ABA           ; A = A + B = 0x30
E005: 8B 01        ADDA #$01     ; A = 0x31
E007: B7 01 00     STAA $0100    ; Store A to RAM at 0x0100
E00A: 20 FE        BRA  *        ; Loop forever
```

**Test Procedure**:
1. Configure memory regions:
   ```
   config rom e000 2000
   config ram 0 200
   ```
2. Load the test program:
   ```
   load
   ```
   Paste contents of `test2_arithmetic.hex`, then:
   ```
   end
   ```
3. Reset and run:
   ```
   reset
   run
   ```
   Wait ~100ms:
   ```
   halt
   ```
4. Check CPU status and memory:
   ```
   status
   read 100 1
   ```

**Expected Results**:
- A = $31
- B = $20
- Memory at $0100 = $31
- CCR flags: Z=0 (not zero), N=0 (positive)

**What This Tests**:
- ✅ ABA instruction (Add B to A)
- ✅ ADDA immediate mode
- ✅ STAA extended addressing (write to memory)
- ✅ Condition code register updates
- ✅ RAM write operations

---

### Test 3: Stack & Subroutines (test3_stack.hex)

**Purpose**: Verify stack pointer operations, push/pull, and subroutine calls.

**Machine Code**:
```
E000: 8E 01 FF     LDS  #$01FF   ; Initialize stack pointer
E003: 86 AA        LDAA #$AA     ; A = 0xAA
E005: 36           PSHA          ; Push A to stack
E006: 86 55        LDAA #$55     ; A = 0x55
E008: 32           PULA          ; Pull A from stack (A = 0xAA)
E009: BD E0 0F     JSR  SUB      ; Call subroutine at $E00F
E00C: 20 FE        BRA  *        ; Loop forever
E00E: (padding)
E00F: C6 BB        LDAB #$BB     ; SUB: B = 0xBB
E011: 39           RTS           ; Return from subroutine
```

**Test Procedure**:
1. Configure memory:
   ```
   config rom e000 2000
   config ram 0 200
   ```
2. Load test program:
   ```
   load
   ```
   Paste contents of `test3_stack.hex`, then:
   ```
   end
   ```
3. Reset and run:
   ```
   reset
   run
   ```
   Wait ~100ms:
   ```
   halt
   status
   ```

**Expected Results**:
- A = $AA (restored from stack)
- B = $BB (set in subroutine)
- SP = $01FF (stack pointer back to initial value)
- PC = $E00C or $E00E (in the final loop)

**What This Tests**:
- ✅ LDS instruction (Load Stack Pointer)
- ✅ PSHA (Push A to stack, SP decrements)
- ✅ PULA (Pull A from stack, SP increments)
- ✅ JSR (Jump to Subroutine, pushes return address)
- ✅ RTS (Return from Subroutine, pulls return address)
- ✅ Stack operations work correctly

---

## Common Commands Reference

- `help` - Show all available commands
- `status` - Display CPU registers and state
- `reset` - Reset CPU (loads PC from $FFFE)
- `run` - Start CPU execution
- `halt` - Stop CPU execution
- `config rom <base> <size>` - Configure ROM region (hex addresses)
- `config ram <base> <size>` - Configure RAM region (hex addresses)
- `read <addr> <len>` - Read memory (hex addresses)
- `write <addr> <bytes...>` - Write to memory (hex values)
- `load` - Enter Intel HEX load mode
- `end` - Finish loading HEX data

## Troubleshooting

**Problem**: Can't connect to USB CDC
- Make sure the firmware was built and flashed correctly
- Try different USB cable
- Check `/dev/tty.usbmodem*` on macOS or `/dev/ttyACM*` on Linux
- On Windows, check Device Manager for COM port

**Problem**: Commands don't work
- Type `help` to see if emulator is responding
- Check that you're using lowercase commands
- Make sure USB CDC is connected (check with `status`)

**Problem**: Wrong values after running test
- Check that you configured ROM/RAM correctly
- Verify the HEX file loaded correctly with `read` command
- Make sure you ran `reset` before `run`
- Try running for less time before `halt`

**Problem**: PC doesn't start at $E000
- The reset vector at $FFFE must point to $E000
- All test HEX files include this reset vector
- Run `reset` command to load PC from reset vector
- Check with `read fffe 2` - should show `E0 00`

## Next Steps

Once all three tests pass:
1. Test with more complex programs
2. Verify instruction timing is correct
3. Test with real pinball ROM code
4. Connect to physical hardware (address/data bus)
5. Add more MC6800 instructions as needed

## Generating New Test Programs

To create additional test programs:

1. Edit `tools/make_test_hex.py`
2. Add your program bytes (see existing examples)
3. Run the script:
   ```bash
   cd tools
   python3 make_test_hex.py
   ```
4. New HEX files will be created in the `tests/` directory
