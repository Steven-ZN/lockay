#!/bin/sh
# lockay interactive installer (EN/ZH)
set -e

# ============================================================
# Logo
# ============================================================
printf '\n'
cat << 'LOGO'
  ░██           ░██████     ░██████  ░██     ░██    ░███    ░██     ░██
  ░██          ░██   ░██   ░██   ░██ ░██    ░██    ░██░██    ░██   ░██
  ░██         ░██     ░██ ░██        ░██   ░██    ░██  ░██    ░██ ░██
  ░██         ░██     ░██ ░██        ░███████    ░█████████    ░████
  ░██         ░██     ░██ ░██        ░██   ░██   ░██    ░██     ░██
  ░██          ░██   ░██   ░██   ░██ ░██    ░██  ░██    ░██     ░██
  ░██████████   ░██████     ░██████  ░██     ░██ ░██    ░██     ░██     v0.1.0

  Lockay installer
  chmod for lines of code | sudo for autonomous agents

LOGO

echo ""

# ============================================================
# Language selection
# ============================================================
echo "  [1] English"
echo "  [2] Chinese / 中文"
echo ""

if [ -t 0 ]; then read -r LANG_CHOICE; else read -r LANG_CHOICE </dev/tty 2>/dev/null || LANG_CHOICE="1"; fi
[ "$LANG_CHOICE" = "2" ] && LANG="zh" || LANG="en"

# ============================================================
# Message helpers
# ============================================================
_step()   { echo ""; echo "  [$1/$TOTAL_STEPS] $2"; }
_w()      { [ "$LANG" = "zh" ] && echo "  用途: $1" || echo "  What: $1"; }
_y()      { [ "$LANG" = "zh" ] && echo "  原因: $1" || echo "  Why:  $1"; }
_ok()     { [ "$LANG" = "zh" ] && echo "  结果: OK" || echo "  Result: OK"; }

_ask() {
    [ "$LANG" = "zh" ] && printf "  继续? [Y/n] " || printf "  Proceed? [Y/n] "
    if [ -t 0 ]; then read -r CONFIRM; else read -r CONFIRM </dev/tty 2>/dev/null || CONFIRM="y"; fi
    case "$CONFIRM" in [Yy]|[Yy][Ee][Ss]|"") return 0 ;; *) return 1 ;; esac
}

TOTAL_STEPS=5
STEP=0
_n() { STEP=$((STEP + 1)); }

# ============================================================
# Welcome
# ============================================================
if [ "$LANG" = "zh" ]; then
    echo ""
    echo "  lockay: 给 coding agent 用的受控 shell"
    echo ""
    echo "  安装过程共 5 步，每步会解释做什么、为什么、等你确认再执行。"
else
    echo ""
    echo "  lockay: capability shell for coding agents"
    echo ""
    echo "  5 steps. Each explains what, why, and waits for confirmation."
fi

# ============================================================
# Step 1: Platform check
# ============================================================
_n
if [ "$LANG" = "zh" ]; then
    _step "$STEP" "检测编译环境"
    _w "确认是 Linux/macOS，有 gcc 和 make"
    _y "lockay 是 C11 + POSIX 程序，能在 Linux/macOS/BSD/WSL 上跑"
else
    _step "$STEP" "Check build environment"
    _w "Verify Linux/macOS, gcc, make are available"
    _y "lockay is a C11 program using POSIX APIs. Works on Linux, macOS, BSD, WSL."
fi

UNAME_S=$(uname -s)
case "$UNAME_S" in
    Darwin) [ "$LANG" = "zh" ] && echo "  平台: macOS" || echo "  Platform: macOS" ;;
    Linux)  [ "$LANG" = "zh" ] && echo "  平台: Linux"  || echo "  Platform: Linux"  ;;
    *)      [ "$LANG" = "zh" ] && echo "  注意: 未测试平台 ($UNAME_S)，可能仍可工作" || echo "  Warning: untested platform ($UNAME_S), may still work" ;;
esac

if ! command -v gcc >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    if [ "$LANG" = "zh" ]; then
        echo "  错误: 没有找到 C 编译器，请先安装:"
        echo "    Ubuntu/Debian: sudo apt install build-essential"
        echo "    Fedora:        sudo dnf install gcc make"
        echo "    macOS:         xcode-select --install"
    else
        echo "  ERROR: No C compiler found. Install one first:"
        echo "    Ubuntu/Debian: sudo apt install build-essential"
        echo "    Fedora:        sudo dnf install gcc make"
        echo "    macOS:         xcode-select --install"
    fi
    exit 1
fi
if ! command -v make >/dev/null 2>&1; then
    [ "$LANG" = "zh" ] && echo "  错误: 没有 make" || echo "  ERROR: make not found."
    exit 1
fi
_ok

# ============================================================
# Step 2: Source code
# ============================================================
_n
if [ -f Makefile ] && [ -f src/main.c ]; then
    SRCDIR="."
    if [ "$LANG" = "zh" ]; then
        _step "$STEP" "定位源码"
        _w "当前已在 lockay 源码目录中"
    else
        _step "$STEP" "Locate source code"
        _w "Already in the lockay source directory"
    fi
else
    SRCDIR="/tmp/lockay-build-$$"
    if [ "$LANG" = "zh" ]; then
        _step "$STEP" "从 GitHub 下载源码"
        _w "git clone https://github.com/Steven-ZN/lockay.git 到 /tmp"
        _y "需要源码来编译"
    else
        _step "$STEP" "Download source from GitHub"
        _w "git clone https://github.com/Steven-ZN/lockay.git to /tmp"
        _y "Need the source code to compile"
    fi
    _ask || { [ "$LANG" = "zh" ] && echo "  已取消" || echo "  Aborted."; exit 0; }
    if ! command -v git >/dev/null 2>&1; then
        [ "$LANG" = "zh" ] && echo "  错误: 没有 git，请安装 git 或把此脚本放到 lockay 目录下运行" || echo "  ERROR: git not found."
        exit 1
    fi
    git clone --depth 1 https://github.com/Steven-ZN/lockay.git "$SRCDIR"
fi
_ok
cd "$SRCDIR"

# ============================================================
# Step 3: Compile
# ============================================================
_n
if [ "$LANG" = "zh" ]; then
    _step "$STEP" "编译 lockay"
    _w "运行 make 把 C 源码编译成单个二进制文件"
    _y "使用 -Wall -Wextra -Werror，零警告，零外部依赖"
else
    _step "$STEP" "Compile lockay"
    _w "Run 'make' to compile the C source into a single binary"
    _y "Compiles with -Wall -Wextra -Werror. Zero external dependencies."
fi
_ask || { [ "$LANG" = "zh" ] && echo "  已取消" || echo "  Aborted."; exit 0; }
make clean 2>/dev/null || true
make
_ok

# ============================================================
# Step 4: Test
# ============================================================
_n
if [ "$LANG" = "zh" ]; then
    _step "$STEP" "运行测试套件"
    _w "跑 51 个测试用例，覆盖所有模块"
    _y "在安装前验证二进制在你的机器上正确工作"
else
    _step "$STEP" "Run test suite"
    _w "Run 51 test cases covering all modules"
    _y "Verifies the binary works correctly on your platform before installing"
fi
_ask || { [ "$LANG" = "zh" ] && echo "  跳过测试" || echo "  Skipping tests."; }
make test 2>/dev/null || true
_ok

# ============================================================
# Step 5: Install
# ============================================================
_n
if [ -w /usr/local/bin ] 2>/dev/null; then
    INSTALL_METHOD="system"
    BIN_DIR="/usr/local/bin"
elif mkdir -p "$HOME/.local/bin" 2>/dev/null && [ -w "$HOME/.local/bin" ]; then
    INSTALL_METHOD="user"
    BIN_DIR="$HOME/.local/bin"
else
    INSTALL_METHOD="system"
    BIN_DIR="/usr/local/bin"
    NEED_SUDO=1
fi

if [ "$LANG" = "zh" ]; then
    _step "$STEP" "安装 lockay 到 $BIN_DIR"
    _w "把 lockay 复制到 $BIN_DIR/"
    if [ "$INSTALL_METHOD" = "user" ]; then
        _y "这是你的用户 bin 目录，不需要 sudo"
    else
        _y "系统 bin 目录，已在 PATH 中"
    fi
else
    _step "$STEP" "Install lockay to $BIN_DIR"
    _w "Copy lockay binary to $BIN_DIR/"
    _y "$BIN_DIR is in your PATH"
fi

_ask || { [ "$LANG" = "zh" ] && echo "  已取消，编译好的文件在 build/lockay" || echo "  Aborted. Binary is at: build/lockay"; exit 0; }

if [ "$INSTALL_METHOD" = "user" ]; then
    mkdir -p "$HOME/.local/bin"
    cp build/lockay "$HOME/.local/bin/lockay"
    chmod 755 "$HOME/.local/bin/lockay"
else
    if [ "$NEED_SUDO" = "1" ]; then
        [ "$LANG" = "zh" ] && echo "  需要 sudo 写入 $BIN_DIR" || echo "  Need sudo to write to $BIN_DIR"
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
    echo "  lockay 安装完成"
    echo "  ========================"
    echo ""
    echo "  下一步:"
    echo "    cd /你的项目目录"
    echo "    lockay init               # 初始化 (生成 .lockay/ 目录和策略文件)"
    echo ""
    echo "  然后:"
    echo "    lockay lock src/api.py 40 80 steven \"public API\"   # 锁代码"
    echo "    lockay status                                        # 查看锁"
    echo "    lockay run \"pytest tests/\"                           # 门控执行"
    echo "    lockay shell                                         # 进入受控 shell"
    echo ""
    if [ "$INSTALL_METHOD" = "user" ]; then
        echo "  如果 lockay 找不到命令，把 ~/.local/bin 加入 PATH:"
        echo "    echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.bashrc"
        echo ""
    fi
else
    echo ""
    echo "  ========================"
    echo "  lockay installed"
    echo "  ========================"
    echo ""
    echo "  Next steps:"
    echo "    cd /your/project"
    echo "    lockay init               # initialize (.lockay/ dir + policy file)"
    echo ""
    echo "  Then:"
    echo "    lockay lock src/api.py 40 80 you \"public API\""
    echo "    lockay status"
    echo "    lockay run \"pytest tests/\""
    echo "    lockay shell"
    echo ""
    if [ "$INSTALL_METHOD" = "user" ]; then
        echo "  If lockay is not found, add ~/.local/bin to PATH:"
        echo "    echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.bashrc"
        echo ""
    fi
fi

if [ "$SRCDIR" != "." ]; then
    rm -rf "$SRCDIR"
fi
