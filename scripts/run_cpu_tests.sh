#!/bin/bash

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="/tmp/linapple_tests"
BIN_URL_6502="https://raw.githubusercontent.com/linappleii/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin"
TRAP_6502="0x336D"
TRAP_65C02="0x2434"

if [ -z "$EMULATOR" ]; then
    if [ -f "$REPO_ROOT/build/linapple" ]; then
        EMULATOR="$REPO_ROOT/build/linapple"
    elif [ -f "$REPO_ROOT/build/bin/linapple" ]; then
        EMULATOR="$REPO_ROOT/build/bin/linapple"
    else
        EMULATOR="build/linapple"
    fi
fi

mkdir -p "$TEST_DIR"

# Download or copy 6502 test binary if not exists
if [ ! -f "$TEST_DIR/6502_functional_test.bin" ]; then
    if [ -f "$REPO_ROOT/tests/fixtures/6502_functional_test.bin" ]; then
        cp "$REPO_ROOT/tests/fixtures/6502_functional_test.bin" "$TEST_DIR/6502_functional_test.bin"
    else
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
fi

# Set up 65C02 extended opcodes test binary
if [ ! -f "$TEST_DIR/65C02_extended_opcodes_test.bin" ]; then
    if [ -f "$REPO_ROOT/tests/fixtures/65C02_extended_opcodes_test.bin" ]; then
        cp "$REPO_ROOT/tests/fixtures/65C02_extended_opcodes_test.bin" "$TEST_DIR/65C02_extended_opcodes_test.bin"
    fi
fi

# Run 6502 Test
echo "Running NMOS 6502 Functional Test..."
RESULT_6502=$($EMULATOR --test-6502 --test-cpu "$TEST_DIR/6502_functional_test.bin" --test-trap "$TRAP_6502" | grep "CPU trapped")
echo "$RESULT_6502"

if [[ "$RESULT_6502" == *"$TRAP_6502"* ]]; then
    echo "6502 Test: PASS"
else
    echo "6502 Test: FAIL"
    exit 1
fi

# Run 65C02 Test
echo "Running CMOS 65C02 Functional Test..."
RESULT_65C02=$($EMULATOR --test-65c02 --test-cpu "$TEST_DIR/65C02_extended_opcodes_test.bin" --test-trap "$TRAP_65C02" | grep "CPU trapped")
echo "$RESULT_65C02"

if [[ "$RESULT_65C02" == *"$TRAP_65C02"* ]]; then
    echo "65C02 Test: PASS"
else
    echo "65C02 Test: FAIL"
    exit 1
fi

echo "All CPU tests passed!"
