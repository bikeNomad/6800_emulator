# Makefile to build 6800 emulator for both board configurations
# Produces BOARD_PICO2 and BOARD_NED_SYS7 .uf2 files in images/ directory

.PHONY: all clean images build_pico2 build_ned_sys7

# Board types to build
BOARDS = BOARD_PICO2 BOARD_NED_SYS7

# Output directory for .uf2 files
IMAGES_DIR = images

# Build directories for each board
BUILD_PICO2_DIR = build_pico2
BUILD_NED_SYS7_DIR = build_ned_sys7

# System clock speed (can be overridden from command line)
# Options: 150-300 MHz (safe range for RP2350 with proper voltage)
SYS_CLOCK_MHZ ?= 266  # Default: 266MHz (optimized for 133MHz QSPI flash)

# QSPI clock divisor (can be overridden from command line)
# Options: 1 (full speed), 2 (half speed), 3, 4, etc.
QSPI_CLOCK_DIVISOR ?= 2  # Default: 133MHz with 266MHz system clock (flash chip max rating)

# UF2 executable name (from CMakeLists.txt)
UF2_NAME = mc6800_emulator.uf2

# Default target: build both configurations
all: $(IMAGES_DIR) build_pico2 build_ned_sys7

# Create images directory
$(IMAGES_DIR):
	mkdir -p $(IMAGES_DIR)

# Build for Raspberry Pi Pico 2
build_pico2: $(IMAGES_DIR)
	@echo "Building for BOARD_PICO2 (SYS: $(SYS_CLOCK_MHZ)MHz, QSPI divisor: $(QSPI_CLOCK_DIVISOR))..."
	mkdir -p $(BUILD_PICO2_DIR)
	cd $(BUILD_PICO2_DIR) && cmake .. -DBOARD_TYPE=BOARD_PICO2 -DSYS_CLOCK_MHZ=$(SYS_CLOCK_MHZ) -DQSPI_CLOCK_DIVISOR=$(QSPI_CLOCK_DIVISOR)
	cd $(BUILD_PICO2_DIR) && make -j$(shell nproc 2>/dev/null || echo 4)
	cp $(BUILD_PICO2_DIR)/$(UF2_NAME) $(IMAGES_DIR)/BOARD_PICO2.uf2
	@echo "BOARD_PICO2 build complete"

# Build for Ned's System 7 Board
build_ned_sys7: $(IMAGES_DIR)
	@echo "Building for BOARD_NED_SYS7 (SYS: $(SYS_CLOCK_MHZ)MHz, QSPI divisor: $(QSPI_CLOCK_DIVISOR))..."
	mkdir -p $(BUILD_NED_SYS7_DIR)
	cd $(BUILD_NED_SYS7_DIR) && cmake .. -DBOARD_TYPE=BOARD_NED_SYS7 -DSYS_CLOCK_MHZ=$(SYS_CLOCK_MHZ) -DQSPI_CLOCK_DIVISOR=$(QSPI_CLOCK_DIVISOR)
	cd $(BUILD_NED_SYS7_DIR) && make -j$(shell nproc 2>/dev/null || echo 4)
	cp $(BUILD_NED_SYS7_DIR)/$(UF2_NAME) $(IMAGES_DIR)/BOARD_NED_SYS7.uf2
	@echo "BOARD_NED_SYS7 build complete"

# Individual board targets (for convenience)
pico2: build_pico2
ned_sys7: build_ned_sys7

# Clean all build directories and images
clean: clean-images
	rm -rf $(BUILD_PICO2_DIR) $(BUILD_NED_SYS7_DIR) build

# Clean images only
clean-images:
	rm -rf $(IMAGES_DIR)/BOARD_*.uf2


# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build both board configurations (default)"
	@echo "  build_pico2  - Build only for BOARD_PICO2"
	@echo "  build_ned_sys7 - Build only for BOARD_NED_SYS7"
	@echo "  pico2        - Alias for build_pico2"
	@echo "  ned_sys7     - Alias for build_ned_sys7"
	@echo "  clean        - Remove all build directories and images"
	@echo "  clean-images - Remove only images directory"
	@echo "  help         - Show this help"
	@echo ""
	@echo "Build options:"
	@echo "  SYS_CLOCK_MHZ=N      - Set system clock speed in MHz (default: 266)"
	@echo "                         Safe range: 150-300 MHz (RP2350 with proper voltage)"
	@echo "                         Use: make SYS_CLOCK_MHZ=250 build_pico2"
	@echo "  QSPI_CLOCK_DIVISOR=N - Set QSPI bus clock divisor (default: 2)"
	@echo "                         With 266MHz sys clock: 1=266MHz, 2=133MHz, 3=88MHz, 4=66MHz"
	@echo "                         Flash chip rated at 133MHz maximum"
	@echo "                         Use: make QSPI_CLOCK_DIVISOR=4 build_pico2"
	@echo ""
	@echo "Examples:"
	@echo "  make SYS_CLOCK_MHZ=250 QSPI_CLOCK_DIVISOR=2 build_pico2  # 250MHz sys, 125MHz QSPI"
	@echo ""
	@echo "Output files will be placed in $(IMAGES_DIR)/"
	@echo "  - $(IMAGES_DIR)/BOARD_PICO2.uf2"
	@echo "  - $(IMAGES_DIR)/BOARD_NED_SYS7.uf2"

micropython-pio:
	mpremote cp micropython/board_config.py :
	mpremote cp micropython/bus_cycle_pio.py :
	mpremote cp micropython/clock_pio.py :
	mpremote cp micropython/sm_helpers.py :
	mpremote cp micropython/main_pio_test.py :main.py