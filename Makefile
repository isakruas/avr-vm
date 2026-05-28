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

# Discover tests/test_*.asm.
ASM_SRCS  = $(wildcard $(TEST_DIR)/test_*.asm)

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

# Compile any src/*.c into obj/*.o (recompile when the headers change).
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/avr_core.h $(SRC_DIR)/avr_devices.h
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

# C tests: run every C test binary for every MCU listed by the VM.
test-c: $(TEST_BINS) $(BIN_DIR)/$(TARGET)
	@fail=0; pass=0; skip=0; total=0; \
	printf ":020000000000FE\n:00000001FF\n" > /tmp/nop.hex; \
	MCUS=$$($(BIN_DIR)/$(TARGET) /tmp/nop.hex --list-mcus 2>/dev/null | awk 'NR>1 {print $$1}'); \
	for m in $$MCUS; do \
	  meta=$$(awk -v m="$$m" '/^[[:space:]]*\{"/ { \
	    line=$$0; gsub(/[{}"]/,"",line); n=split(line,a,","); \
	    for(i=1;i<=n;i++){gsub(/^[[:space:]]+|[[:space:]]+$$/,"",a[i])} \
	    if (a[1]==m) { print a[2],a[3],a[4],a[7],a[8]; exit } \
	  }' $(SRC_DIR)/avr_devices.h); \
	  arch=$$(echo $$meta | awk '{print $$1}'); \
	  core=$$(echo $$meta | awk '{print $$2}'); \
	  flash=$$(echo $$meta | awk '{print $$3}'); \
	  ramstart_hex=$$(echo $$meta | awk '{print $$4}'); \
	  ramend_hex=$$(echo $$meta | awk '{print $$5}'); \
	  ramstart=$$((ramstart_hex)); \
	  ramend=$$((ramend_hex)); \
	  for t in $(TEST_BINS); do \
	    name=$$(basename $$t); do_run=1; \
	    case "$$name" in \
	      test_des|test_xmega) [ "$$core" = "AVR_CORE_XM" ] || do_run=0 ;; \
	      test_mul) [ "$$core" = "AVR_CORE_EP" ] || [ "$$core" = "AVR_CORE_XT" ] || [ "$$core" = "AVR_CORE_XM" ] || do_run=0 ;; \
	      test_lpm) [ "$$core" = "AVR_CORE_EP" ] || [ "$$core" = "AVR_CORE_XM" ] || do_run=0; [ $$flash -le 65536 ] && do_run=0 ;; \
	      test_branch) [ "$$core" = "AVR_CORE_RC" ] && do_run=0; [ $$ramstart -ne 256 ] && do_run=0; [ $$ramend -lt 16639 ] && do_run=0 ;; \
	      test_pointer) [ "$$core" = "AVR_CORE_RC" ] && do_run=0; [ $$ramstart -gt 512 ] && do_run=0; [ $$ramend -lt 769 ] && do_run=0 ;; \
	      test_transfer) [ "$$core" = "AVR_CORE_EP" ] || [ "$$core" = "AVR_CORE_XT" ] || [ "$$core" = "AVR_CORE_XM" ] || do_run=0; [ $$ramstart -gt 768 ] && do_run=0; [ $$ramend -lt 16639 ] && do_run=0 ;; \
	      test_arithmetic) [ "$$core" = "AVR_CORE_RC" ] && do_run=0 ;; \
	    esac; \
	    if [ $$do_run -eq 0 ]; then skip=$$((skip + 1)); continue; fi; \
	    total=$$((total + 1)); \
	    if AVR_TEST_MCU=$$m $$t >/dev/null; then \
	        echo "[mcu=$$m $$(basename $$t)] PASS"; \
	        pass=$$((pass + 1)); \
	    else \
	        echo "[mcu=$$m $$(basename $$t)] FAIL"; \
	        fail=$$((fail + 1)); \
	    fi; \
	  done; \
	done; \
	echo ""; \
	echo "=================================================="; \
	echo "C test summary: $$pass / $$total runs passed ($$fail failures, $$skip skipped)"; \
	echo "=================================================="; \
	[ $$fail -eq 0 ]

# ASM tests: for each MCU, compile each ASM with that MCU's -mmcu and run it.
test-asm: $(BIN_DIR)/$(TARGET)
	@fail=0; pass=0; skip=0; total=0; \
	mkdir -p $(TEST_BIN); \
	printf ":020000000000FE\n:00000001FF\n" > $(TEST_BIN)/_nop.hex; \
	MCUS=$$($(BIN_DIR)/$(TARGET) $(TEST_BIN)/_nop.hex --list-mcus 2>/dev/null | awk 'NR>1 {print $$1}'); \
	for m in $$MCUS; do \
	  meta=$$(awk -v m="$$m" '/^[[:space:]]*\{"/ { \
	    line=$$0; gsub(/[{}"]/,"",line); n=split(line,a,","); \
	    for(i=1;i<=n;i++){gsub(/^[[:space:]]+|[[:space:]]+$$/,"",a[i])} \
	    if (a[1]==m) { print a[2],a[3],a[4],a[7],a[8]; exit } \
	  }' $(SRC_DIR)/avr_devices.h); \
	  arch=$$(echo $$meta | awk '{print $$1}'); \
	  core=$$(echo $$meta | awk '{print $$2}'); \
	  flash=$$(echo $$meta | awk '{print $$3}'); \
	  ramstart_hex=$$(echo $$meta | awk '{print $$4}'); \
	  ramend_hex=$$(echo $$meta | awk '{print $$5}'); \
	  ramstart=$$((ramstart_hex)); \
	  ramend=$$((ramend_hex)); \
	  for s in $(ASM_SRCS); do \
	    name=$$(basename $$s .asm); \
	    do_run=1; \
	    case "$$name" in \
	      test_des) [ "$$core" = "AVR_CORE_XM" ] || do_run=0 ;; \
	      test_mul) [ "$$core" = "AVR_CORE_EP" ] || [ "$$core" = "AVR_CORE_XT" ] || [ "$$core" = "AVR_CORE_XM" ] || do_run=0 ;; \
	      test_transfer) [ "$$core" = "AVR_CORE_EP" ] || [ "$$core" = "AVR_CORE_XT" ] || [ "$$core" = "AVR_CORE_XM" ] || do_run=0; [ $$ramstart -gt 768 ] && do_run=0; [ $$ramend -lt 16639 ] && do_run=0 ;; \
	      test_alu) [ "$$core" = "AVR_CORE_RC" ] && do_run=0; [ $$arch -le 2 ] && do_run=0 ;; \
	      test_branch) [ "$$core" = "AVR_CORE_RC" ] && do_run=0; [ $$ramstart -ne 256 ] && do_run=0; [ $$ramend -lt 16639 ] && do_run=0 ;; \
	      test_bitops) [ "$$core" = "AVR_CORE_RC" ] && do_run=0 ;; \
	    esac; \
	    if [ $$do_run -eq 0 ]; then \
	        echo "[mcu=$$m $$name] SKIP"; \
	        skip=$$((skip + 1)); \
	        continue; \
	    fi; \
	    total=$$((total + 1)); \
	    elf=$(TEST_BIN)/$$name.$$m.elf; \
	    hex=$(TEST_BIN)/$$name.$$m.hex; \
	    if $(AVR_CC) -mmcu=$$m -nostartfiles -x assembler-with-cpp -o $$elf $$s >/dev/null 2>&1 \
	       && $(OBJCOPY) -O ihex $$elf $$hex >/dev/null 2>&1 \
	       && $(BIN_DIR)/$(TARGET) $$hex -mmcu=$$m -n $(ASM_BUDGET) -d 2>/dev/null | grep -q "R16 = 0x42"; then \
	        echo "[mcu=$$m $$name] PASS"; \
	        pass=$$((pass + 1)); \
	    else \
	        echo "[mcu=$$m $$name] FAIL"; fail=$$((fail + 1)); \
	    fi; \
	  done; \
	done; \
	rm -f $(TEST_BIN)/_nop.hex; \
	echo ""; \
	echo "=================================================="; \
	echo "ASM test summary: $$pass / $$total runs passed ($$fail failures, $$skip skipped)"; \
	echo "=================================================="; \
	[ $$fail -eq 0 ]

# Remove all generated objects and binaries.
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
