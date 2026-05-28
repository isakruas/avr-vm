# Build configuration for the AVR-VM core, the CLI driver, and the tests.

TARGET    = avr_vm

# Source tree layout.
SRC_DIR   = src
OBJ_DIR   = obj
BIN_DIR   = bin
TEST_DIR  = tests
TEST_BIN  = $(BIN_DIR)/tests

CC        = gcc
CFLAGS    = -Wall -Wextra -std=c99 -g -O2 -I$(SRC_DIR)
CHECK_CFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Werror -std=c99 -I$(SRC_DIR) -I$(TEST_DIR) -fsyntax-only
LDFLAGS   =
CLANG_FORMAT = clang-format

# AVR toolchain used to assemble the .asm tests into Intel-HEX images.
AVR_CC    = avr-gcc
# XMEGA target so the full instruction set (incl. DES) assembles; the VM
# decodes by opcode, and shared instructions encode identically across cores.
AVR_FLAGS = -mmcu=atxmega128a1 -nostartfiles -x assembler-with-cpp
OBJCOPY   = avr-objcopy
ASM_BUDGET = 100000        # instruction limit when running an .asm test

# Core object is shared by the CLI and every test binary.
CORE_OBJ  = $(OBJ_DIR)/avr_core.o
MAIN_OBJ  = $(OBJ_DIR)/main.o

# Discover tests/test_*.c and map each to a binary under bin/tests/.
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(TEST_BIN)/%,$(TEST_SRCS))
C_FORMAT_SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*.h) \
                $(wildcard $(TEST_DIR)/*.c) $(wildcard $(TEST_DIR)/*.h)

# Discover tests/test_*.asm and map each to a .hex image under bin/tests/.
ASM_SRCS  = $(wildcard $(TEST_DIR)/test_*.asm)
ASM_HEXS  = $(patsubst $(TEST_DIR)/%.asm,$(TEST_BIN)/%.hex,$(ASM_SRCS))

.PHONY: all clean lint check-c test test-c test-asm

all: $(BIN_DIR)/$(TARGET)

# Format C sources and headers in place.
lint:
	@command -v $(CLANG_FORMAT) >/dev/null || { echo "clang-format is required for make lint"; exit 1; }
	$(CLANG_FORMAT) -i $(C_FORMAT_SRCS)

# Static C checks with compiler warnings enabled; no objects are generated.
check-c:
	$(CC) $(CHECK_CFLAGS) $(wildcard $(SRC_DIR)/*.c) $(TEST_SRCS)

# Link the CLI from the driver and the core object.
$(BIN_DIR)/$(TARGET): $(MAIN_OBJ) $(CORE_OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)

# Compile any src/*.c into obj/*.o (recompile when the public header changes).
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/avr_core.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build each C test binary, linking its source against the core object.
$(TEST_BIN)/%: $(TEST_DIR)/%.c $(CORE_OBJ) $(TEST_DIR)/test_common.h $(SRC_DIR)/avr_core.h
	@mkdir -p $(TEST_BIN)
	$(CC) $(CFLAGS) -I$(TEST_DIR) $< $(CORE_OBJ) -o $@ $(LDFLAGS)

# Assemble each .asm test into an Intel-HEX image the VM can load.
$(TEST_BIN)/%.hex: $(TEST_DIR)/%.asm
	@mkdir -p $(TEST_BIN)
	$(AVR_CC) $(AVR_FLAGS) -o $(TEST_BIN)/$*.elf $<
	$(OBJCOPY) -O ihex $(TEST_BIN)/$*.elf $@

# Run both test suites.
test: test-c test-asm

# C tests: run every binary and print an aggregate pass/fail summary.
test-c: $(TEST_BINS)
	@fail=0; pass=0; total=0; \
	for t in $(TEST_BINS); do \
	    total=$$((total + 1)); \
	    if $$t; then pass=$$((pass + 1)); \
	    else fail=$$((fail + 1)); fi; \
	done; \
	echo ""; \
	echo "=================================================="; \
	echo "C test summary: $$pass / $$total binaries passed ($$fail failures)"; \
	echo "=================================================="; \
	[ $$fail -eq 0 ]

# ASM tests: assemble, run in the VM under an instruction budget, and check
# that the program reached its success path (R16 = 0x42 in the dump).
test-asm: $(ASM_HEXS) $(BIN_DIR)/$(TARGET)
	@fail=0; pass=0; total=0; \
	for h in $(ASM_HEXS); do \
	    total=$$((total + 1)); \
	    name=$$(basename $$h .hex); \
	    if $(BIN_DIR)/$(TARGET) $$h -n $(ASM_BUDGET) -d 2>/dev/null | grep -q "R16 = 0x42"; then \
	        echo "[$$name] PASS"; pass=$$((pass + 1)); \
	    else \
	        echo "[$$name] FAIL"; fail=$$((fail + 1)); \
	    fi; \
	done; \
	echo ""; \
	echo "=================================================="; \
	echo "ASM test summary: $$pass / $$total programs passed ($$fail failures)"; \
	echo "=================================================="; \
	[ $$fail -eq 0 ]

# Remove all generated objects and binaries.
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
