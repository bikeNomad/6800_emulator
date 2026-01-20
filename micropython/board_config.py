# Board configuration constants (from board_config.h)
# Ned's System 7 Board configuration
GPIO_DATA_BASE = 0  # GPIO0..7 are data lines D0..D7
GPIO_ADDR_BASE = 8  # GPIO8..23 are address lines A0..A15
GPIO_E_CLOCK = 24
GPIO_VMA = 25
GPIO_RW = 26
GPIO_IRQ = 27
GPIO_NMI = 28
GPIO_RESET = 29
GPIO_TEST_PIN = 30
GPIO_ECLOCK_IN = 31  # E clock input for external clock (SPARE_IN)

ADDR_LINES = 16
N_ADDR_DATA_PINS = 8 + ADDR_LINES  # 8 data pins + 16 address pins

# PIO config
ECLOCK_PIO = 0  # Eclock and Sync
ECLOCK_SM = 0
SYNC_PIO = 0
SYNC_SM = 1
BUS_CYCLE_PIO = 1
BUS_CYCLE_SM = 0
