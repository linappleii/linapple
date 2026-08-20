#!/bin/bash

# Configuration
TEST_DIR="/tmp/linapple_tests"
BIN_URL_6502="https://raw.githubusercontent.com/linappleii/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin"
SUCCESS_ADDR="0x3469"
EMULATOR=${EMULATOR:-"build/bin/linapple"}

mkdir -p "$TEST_DIR"

# Download test binary if not exists
if [ ! -f "$TEST_DIR/6502_functional_test.bin" ]; then
    echo "Downloading 6502 functional test..."
    if command -v curl &>/dev/null; then
        curl -sL "$BIN_URL_6502" -o "$TEST_DIR/6502_functional_test.bin"
    elif command -v wget &>/dev/null; then
        wget -q -O "$TEST_DIR/6502_functional_test.bin" "$BIN_URL_6502"
    elif command -v python3 &>/dev/null; then
        python3 -c "import urllib.request; urllib.request.urlretrieve('$BIN_URL_6502', '$TEST_DIR/6502_functional_test.bin')"
    elif command -v python &>/dev/null; then
        python -c "import urllib.request; urllib.request.urlretrieve('$BIN_URL_6502', '$TEST_DIR/6502_functional_test.bin')"
    else
        echo "Error: Neither curl, wget, nor python available to download test binary."
        exit 1
    fi
fi

# Run 6502 Test
echo "Running NMOS 6502 Functional Test..."
RESULT_6502=$($EMULATOR --test-6502 --test-cpu "$TEST_DIR/6502_functional_test.bin" --test-trap 0x336D | grep "CPU trapped")
echo "$RESULT_6502"

if [[ "$RESULT_6502" == *"0x336D"* ]]; then
    echo "6502 Test: PASS"
else
    echo "6502 Test: FAIL"
    exit 1
fi

# Run 65C02 Test (using same binary as Apple //e 65C02 doesn't have Rockwell extensions)
echo "Running CMOS 65C02 Functional Test..."
RESULT_65C02=$($EMULATOR --test-65c02 --test-cpu "$TEST_DIR/6502_functional_test.bin" --test-trap 0x3469 | grep "CPU trapped")
echo "$RESULT_65C02"

if [[ "$RESULT_65C02" == *"0x3469"* ]]; then
    echo "65C02 Test: PASS"
else
    echo "65C02 Test: FAIL"
    exit 1
fi

echo "All CPU tests passed!"
