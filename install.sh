#!/bin/sh
# lockay interactive installer
# Usage: curl -fsSL https://raw.githubusercontent.com/Steven-ZN/lockay/main/install.sh | sh

set -e

# ============================================================
# Language selection
# ============================================================
echo ""
echo "  lockay installer v0.1.0"
echo "  ======================="
echo ""
echo "  Select language / :"
echo "    [1] English"
echo "    [2]"
echo ""

if [ -t 0 ]; then
    read -r LANG_CHOICE
else
    read -r LANG_CHOICE </dev/tty 2>/dev/null || LANG_CHOICE="1"
fi

if [ "$LANG_CHOICE" = "2" ]; then
    LANG="zh"
else
    LANG="en"
fi

# ============================================================
# Message helpers
# ============================================================
msg_step() {
    if [ "$LANG" = "zh" ]; then
        echo ""
        echo "  [$1/$TOTAL_STEPS] $2"
        echo "  $(echo "$2" | sed 's/./-/g')"
    else
        echo ""
        echo "  [$1/$TOTAL_STEPS] $2"
    fi
}

msg_what() {
    if [ "$LANG" = "zh" ]; then
        echo "  : $1"
    else
        echo "  What: $1"
    fi
}

msg_why() {
    if [ "$LANG" = "zh" ]; then
        echo "  : $1"
    else
        echo "  Why:  $1"
    fi
}

ask_confirm() {
    if [ "$LANG" = "zh" ]; then
        printf "  ? [Y/n] "
    else
        printf "  Proceed? [Y/n] "
    fi
    if [ -t 0 ]; then
        read -r CONFIRM
    else
        read -r CONFIRM </dev/tty 2>/dev/null || CONFIRM="y"
    fi
    case "$CONFIRM" in
        [Yy]|[Yy][Ee][Ss]|"") return 0 ;;
        *) return 1 ;;
    esac
}

TOTAL_STEPS=5
CURRENT_STEP=0

next_step() {
    CURRENT_STEP=$((CURRENT_STEP + 1))
}

# ============================================================
# Welcome
# ============================================================
if [ "$LANG" = "zh" ]; then
    echo ""
    echo "  lockay:  "
    echo ""
    echo "  5 "
    echo "   "
    echo ""
else
    echo ""
    echo "  lockay: capability shell for coding agents"
    echo ""
    echo "  This installer will run 5 steps."
    echo "  Each step is explained before execution."
    echo ""
fi

# ============================================================
# Step 1: Platform check
# ============================================================
next_step
if [ "$LANG" = "zh" ]; then
    msg_step "$CURRENT_STEP" ""
    msg_what "Linux/macOS  gcc make"
    msg_why "lockay  C11  POSIX Linux  macOS  BSD  WSL"
else
    msg_step "$CURRENT_STEP" "Check build environment"
    msg_what "Verify Linux/macOS, gcc, make are available"
    msg_why "lockay is a C11 program using POSIX APIs. Works on Linux, macOS, BSD, WSL."
fi

UNAME_S=$(uname -s)
if [ "$UNAME_S" = "Darwin" ]; then
    if [ "$LANG" = "zh" ]; then
        echo "  : macOS"
    else
        echo "  Detected: macOS"
    fi
elif [ "$UNAME_S" = "Linux" ]; then
    if [ "$LANG" = "zh" ]; then
        echo "  : Linux"
    else
        echo "  Detected: Linux"
    fi
else
    if [ "$LANG" = "zh" ]; then
        echo "  /: $UNAME_S  "
    else
        echo "  Warning: untested platform ($UNAME_S), may still work"
    fi
fi

if ! command -v gcc >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    if [ "$LANG" = "zh" ]; then
        echo ""
        echo "  :  C  "
        echo "    Ubuntu/Debian: sudo apt install build-essential"
        echo "    Fedora:        sudo dnf install gcc make"
        echo "    macOS:         xcode-select --install"
    else
        echo ""
        echo "  ERROR: No C compiler found. Install one first:"
        echo "    Ubuntu/Debian: sudo apt install build-essential"
        echo "    Fedora:        sudo dnf install gcc make"
        echo "    macOS:         xcode-select --install"
    fi
    exit 1
fi

if ! command -v make >/dev/null 2>&1; then
    if [ "$LANG" = "zh" ]; then
        echo "  :  make"
    else
        echo "  ERROR: make not found."
    fi
    exit 1
fi

if [ "$LANG" = "zh" ]; then
    echo "  : OK"
else
    echo "  Result: OK"
fi

# ============================================================
# Step 2: Source acquisition
# ============================================================
next_step
if [ -f Makefile ] && [ -f src/main.c ]; then
    SRCDIR="."
    if [ "$LANG" = "zh" ]; then
        msg_step "$CURRENT_STEP" ""
        msg_what ""
    else
        msg_step "$CURRENT_STEP" "Locate source code"
        msg_what "Already in the lockay source directory"
    fi
else
    SRCDIR="/tmp/lockay-build-$$"
    if [ "$LANG" = "zh" ]; then
        msg_step "$CURRENT_STEP" "GitHub "
        msg_what "git clone https://github.com/Steven-ZN/lockay.git  /tmp"
        msg_why " "
    else
        msg_step "$CURRENT_STEP" "Download source from GitHub"
        msg_what "git clone https://github.com/Steven-ZN/lockay.git to /tmp"
        msg_why "Need the source code to compile from"
    fi

    if ! ask_confirm; then
        if [ "$LANG" = "zh" ]; then echo "  "; else echo "  Aborted."; fi
        exit 0
    fi

    if ! command -v git >/dev/null 2>&1; then
        if [ "$LANG" = "zh" ]; then
            echo "  :  git git clone   install.sh"
            echo "    Ubuntu: sudo apt install git"
        else
            echo "  ERROR: git not found. Install git, or run this script from a local lockay directory."
            echo "    Ubuntu: sudo apt install git"
        fi
        exit 1
    fi
    git clone --depth 1 https://github.com/Steven-ZN/lockay.git "$SRCDIR"
    if [ "$LANG" = "zh" ]; then echo "  : OK"; else echo "  Result: OK"; fi
fi

cd "$SRCDIR"

# ============================================================
# Step 3: Compile
# ============================================================
next_step
if [ "$LANG" = "zh" ]; then
    msg_step "$CURRENT_STEP" ""
    msg_what "make  lockay  C  C11  POSIX API"
    msg_why "make -Wall -Wextra -Werror  0 "
else
    msg_step "$CURRENT_STEP" "Compile lockay"
    msg_what "Run 'make' to compile the C source into a single binary"
    msg_why "Compiles with -Wall -Wextra -Werror. Zero warnings, zero external dependencies."
fi

if ! ask_confirm; then
    if [ "$LANG" = "zh" ]; then echo "  "; else echo "  Aborted."; fi
    exit 0
fi

make clean 2>/dev/null || true
make
if [ "$LANG" = "zh" ]; then echo "  : OK"; else echo "  Result: OK"; fi

# ============================================================
# Step 4: Run tests
# ============================================================
next_step
if [ "$LANG" = "zh" ]; then
    msg_step "$CURRENT_STEP" ""
    msg_what " 51  SHA-256 "
    msg_why "    "
else
    msg_step "$CURRENT_STEP" "Run test suite"
    msg_what "Run 51 test cases covering all modules"
    msg_why "Verifies the binary works correctly on your platform before installing"
fi

if ! ask_confirm; then
    if [ "$LANG" = "zh" ]; then echo "  "; else echo "  Skipping tests."; fi
else
    make test
    if [ "$LANG" = "zh" ]; then echo "  :  "; else echo "  Result: all tests passed"; fi
fi

# ============================================================
# Step 5: Install binary
# ============================================================
next_step

# Determine install method
if [ -w /usr/local/bin ] 2>/dev/null; then
    INSTALL_METHOD="system"
    BIN_DIR="/usr/local/bin"
elif [ -w "$HOME/.local/bin" ] || mkdir -p "$HOME/.local/bin" 2>/dev/null; then
    INSTALL_METHOD="user"
    BIN_DIR="$HOME/.local/bin"
else
    INSTALL_METHOD="system"
    BIN_DIR="/usr/local/bin"
    NEED_SUDO=1
fi

if [ "$LANG" = "zh" ]; then
    msg_step "$CURRENT_STEP" ""
    if [ "$INSTALL_METHOD" = "user" ]; then
        msg_what " lockay  $HOME/.local/bin/ (sudo)"
        msg_why "$HOME/.local/bin  PATH"
    else
        msg_what " lockay  /usr/local/bin/  PATH"
        msg_why " /usr/local/bin  PATH"
    fi
else
    msg_step "$CURRENT_STEP" "Install binary"
    if [ "$INSTALL_METHOD" = "user" ]; then
        msg_what "Copy lockay to $HOME/.local/bin/ (no sudo needed)"
        msg_why "$HOME/.local/bin is your personal bin directory"
    else
        msg_what "Copy lockay to $BIN_DIR (in system PATH)"
        msg_why "$BIN_DIR is in the standard system PATH"
    fi
fi

if ! ask_confirm; then
    if [ "$LANG" = "zh" ]; then
        echo "  "
        echo "  : build/lockay"
    else
        echo "  Aborted."
        echo "  Binary is at: build/lockay"
    fi
    exit 0
fi

if [ "$INSTALL_METHOD" = "user" ]; then
    mkdir -p "$HOME/.local/bin"
    cp build/lockay "$HOME/.local/bin/lockay"
    chmod 755 "$HOME/.local/bin/lockay"
else
    if [ "$NEED_SUDO" = "1" ]; then
        if [ "$LANG" = "zh" ]; then
            echo "  : sudo "
        else
            echo "  Need sudo to write to $BIN_DIR"
        fi
        sudo cp build/lockay "$BIN_DIR/lockay"
        sudo chmod 755 "$BIN_DIR/lockay"
    else
        cp build/lockay "$BIN_DIR/lockay"
        chmod 755 "$BIN_DIR/lockay"
    fi
fi

# ============================================================
# Done
# ============================================================
if [ "$LANG" = "zh" ]; then
    echo ""
    echo "  ========================"
    echo "  lockay  "
    echo "  ========================"
    echo ""
    echo "  :"
    echo "    cd /path/to/your/repo"
    echo "    lockay policy             # "
    echo "    lockay status              # "
    echo "    lockay edit README.md      #  TUI "
    echo ""
    echo "  agent :"
    echo "    lockay show src/main.c 1 50"
    echo "    lockay set src/main.c 42 'new line'"
    echo "    lockay run 'pytest tests/'"
    echo ""
    echo "  :"
    echo "    ~/.local/bin/ $PATH   ~/.bashrc :"
    echo "    echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.bashrc"
    echo ""
else
    echo ""
    echo "  ========================"
    echo "  lockay installed"
    echo "  ========================"
    echo ""
    echo "  Quick start:"
    echo "    cd /path/to/your/repo"
    echo "    lockay policy             # generate command policy"
    echo "    lockay status              # check lock status"
    echo "    lockay edit README.md      # open TUI editor"
    echo ""
    echo "  Agent-facing:"
    echo "    lockay show src/main.c 1 50"
    echo "    lockay set src/main.c 42 'new line'"
    echo "    lockay run 'pytest tests/'"
    echo ""
    echo "  Note: if $BIN_DIR is not in your PATH, add this to ~/.bashrc:"
    echo "    export PATH=\"$BIN_DIR:\$PATH\""
    echo ""
fi

# Cleanup clone if we made one
if [ "$SRCDIR" != "." ]; then
    rm -rf "$SRCDIR"
fi
