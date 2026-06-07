#!/bin/sh
# lockay one-line installer
# Usage: curl -fsSL https://raw.githubusercontent.com/Steven-ZN/lockay/main/install.sh | sh

set -e

BIN_DIR="${LOCKAY_BIN:-/usr/local/bin}"
MAN_DIR="${LOCKAY_MAN:-/usr/local/share/man/man1}"
VERSION="0.1.0"

echo "lockay installer v${VERSION}"
echo "====================="

# Detect platform
UNAME_S=$(uname -s)
if [ "$UNAME_S" != "Linux" ] && [ "$UNAME_S" != "Darwin" ]; then
    echo "Warning: lockay is tested on Linux and macOS. Your platform ($UNAME_S) may work but is untested."
fi

# Check for compiler
if ! command -v gcc >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    echo "ERROR: No C compiler found. Install gcc or clang first."
    echo "  Ubuntu/Debian: sudo apt install build-essential"
    echo "  Fedora:        sudo dnf install gcc make"
    echo "  macOS:         xcode-select --install"
    exit 1
fi

if ! command -v make >/dev/null 2>&1; then
    echo "ERROR: make not found. Install make first."
    exit 1
fi

# Determine source directory
if [ -f Makefile ] && [ -f src/main.c ]; then
    # Running from within the repo
    SRCDIR="."
else
    # Clone from GitHub
    SRCDIR="/tmp/lockay-build-$$"
    echo "Cloning lockay..."
    if command -v git >/dev/null 2>&1; then
        git clone --depth 1 https://github.com/Steven-ZN/lockay.git "$SRCDIR"
    else
        echo "ERROR: git not found. Either install git or run this script from a local lockay directory."
        exit 1
    fi
fi

cd "$SRCDIR"

# Build
echo "Building lockay..."
make clean 2>/dev/null || true
make

# Install binary
echo "Installing to $BIN_DIR..."
if [ -w "$BIN_DIR" ]; then
    cp build/lockay "$BIN_DIR/lockay"
else
    echo "Need sudo to install to $BIN_DIR"
    sudo cp build/lockay "$BIN_DIR/lockay"
    sudo chmod 755 "$BIN_DIR/lockay"
fi

# Verify
if command -v lockay >/dev/null 2>&1; then
    echo ""
    echo "lockay installed successfully!"
    echo ""
    echo "Try it out:"
    echo "  cd /path/to/your/repo"
    echo "  lockay policy            # generate default command policy"
    echo "  lockay status            # check lock status"
    echo "  lockay edit README.md    # open the TUI editor"
    echo ""
    echo "For agent use:"
    echo "  lockay show src/main.c 1 50"
    echo "  lockay set src/main.c 42 'new code'"
    echo "  lockay run 'pytest tests/'"
else
    echo "WARNING: lockay may not be in your PATH."
    echo "Binary installed at: $BIN_DIR/lockay"
    echo "Add $BIN_DIR to your PATH or run:"
    echo "  export PATH=\"$BIN_DIR:\$PATH\""
fi

# Cleanup
if [ "$SRCDIR" != "." ]; then
    rm -rf "$SRCDIR"
fi
