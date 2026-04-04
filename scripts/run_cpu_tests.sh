#!/bin/bash

# Configuration
TEST_DIR="/tmp/linapple_tests"
BIN_URL_6502="https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin"
SUCCESS_ADDR="0x3469"
EMULATOR="build/bin/linapple"

mkdir -p "$TEST_DIR"

# Download test binary if not exists
if [ ! -f "$TEST_DIR/6502_functional_test.bin" ]; then
    echo "Downloading 6502 functional test..."
    curl -L "$BIN_URL_6502" -o "$TEST_DIR/6502_functional_test.bin"
fi

# Run 6502 Test
echo "Running NMOS 6502 Functional Test..."
RESULT_6502=$($EMULATOR --test-6502 --test-cpu "$TEST_DIR/6502_functional_test.bin" | grep "CPU trapped")
echo "$RESULT_6502"

if [[ "$RESULT_6502" == *"$SUCCESS_ADDR"* ]]; then
    echo "6502 Test: PASS"
else
    echo "6502 Test: FAIL"
    exit 1
fi

# Run 65C02 Test (using same binary as Apple //e 65C02 doesn't have Rockwell extensions)
echo "Running CMOS 65C02 Functional Test..."
RESULT_65C02=$($EMULATOR --test-65c02 --test-cpu "$TEST_DIR/6502_functional_test.bin" | grep "CPU trapped")
echo "$RESULT_65C02"

if [[ "$RESULT_65C02" == *"$SUCCESS_ADDR"* ]]; then
    echo "65C02 Test: PASS"
else
    echo "65C02 Test: FAIL"
    exit 1
fi

echo "All CPU tests passed!"
