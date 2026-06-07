# lockay

**chmod for lines of code. sudo for autonomous agents.**

lockay is a capability layer for coding agents. It enforces what an agent can and cannot do at two levels:

- **linelock** -- file region lock: control which lines of source code can be modified
- **cmdlock** -- command execution gate: control which shell commands can be executed

Together they form a lightweight Unix-style permission system for the agent era.

## Architecture

```
                 +--------------+
                 |   LLM Agent   |
                 +------+-------+
                        |
                        v
                 +--------------+
                 |    lockay     |
                 | controlled sh |
                 +------+-------+
                        |
          +-------------+-------------+
          |                           |
          v                           v
   +--------------+            +--------------+
   |   linelock    |            |   cmdlock     |
   | file mutation |            | command gate  |
   +--------------+            +--------------+
          |                           |
          v                           v
   protected files              privileged actions
```

The agent has no direct write access to repository files and no unrestricted shell access. All writes go through linelock (which validates against locked regions). All commands go through cmdlock (which checks against the policy engine).

## Features

### File Lock (linelock)

- Content-hash-based lock anchoring -- locks survive line number shifts
- Atomic file writes (write temp, fsync, rename)
- nano-style TUI editor with lock visualization
- Unified diff patch application with lock validation
- CLI interface for agent tool-calling

### Command Lock (cmdlock)

- 5-level risk classification (L0 safe read to L4 system mutation)
- Glob-based policy rules (first match wins)
- Interactive approval prompt for ASK-level commands
- Audit logging of all executed commands
- Default deny for dangerous operations (git push, sudo, ssh, chmod)

## Quick Start

### Build from source

```bash
git clone https://github.com/user/lockay.git
cd lockay
make
```

Requirements: gcc, make, Linux. No external dependencies.

### Install

```bash
sudo make install
```

This installs `lockay` to `/usr/local/bin`.

### Basic Usage

```bash
# Set repository root (or use -R flag)
export LOCKAY_ROOT=/path/to/your/repo
cd $LOCKAY_ROOT

# Lock a critical section
lockay lock src/api.py 120 160 steven "public API contract"

# Show file with lock annotations
lockay show src/api.py

# Verify lock integrity
lockay check src/api.py

# Edit a file (nano-style)
lockay edit src/train.py

# Apply a patch (respects locks)
lockay apply src/model.py /tmp/fix.diff

# Unlock a region
lockay unlock <lock_id>
```

### Command Lock

```bash
# Generate default policy file
lockay policy

# Edit .lockay/policy to customize rules

# Run a command through the gate
lockay run "rm -rf build"

# Enter guarded shell
lockay shell
```

### Agent-Facing CLI

For LLM agent integration, these commands return structured exit codes:

```bash
lockay show src/model.py 100 180     # view with lock annotations
lockay set src/model.py 150 "new"    # set a line (exit 0=ok, 1=denied)
lockay insert src/model.py 100 "x"   # insert before line
lockay delete src/model.py 100 105   # delete line range
lockay apply src/model.py patch.diff # apply unified diff
lockay run "pytest tests/"           # run command through gate
```

Exit codes:
- 0 -- success
- 1 -- denied (edit outside allowed region, or command blocked by policy)
- 2 -- error (file not found, etc.)

## Lock Database

Locks are stored in `.linelock/locks` (plain text, git-trackable):

```
# linelock database v1
# file|id|start|end|content_sha256|before_sha256|after_sha256|owner|created|reason
src/api.py|a1b2c3|120|160|abc123...|def456...|ghi789...|steven|2026-01-01T00:00:00|public API contract
```

Each lock stores the SHA-256 hash of the locked content and context lines. This allows the lock to re-anchor itself when line numbers shift due to edits elsewhere in the file.

## Command Policy

Policy rules live in `.lockay/policy`:

```
# Format: <command_pattern>    <allow|ask|deny>
ls *              allow
pytest *          allow
python *          allow
pip install *     ask
rm *              ask
git push *        deny
sudo *            deny
```

Rules use glob matching (first match wins). Default policy is built-in if no policy file exists.

### Risk Levels

| Level | Name | Examples | Default |
|-------|------|----------|---------|
| L0 | Safe | ls, cat, grep, echo, find | Allow |
| L1 | Exec | python, make, gcc, pytest | Allow |
| L2 | Write | touch, mkdir, cp, mv, git add | Allow/Ask |
| L3 | Destruct | rm, pip install, curl, git commit | Ask |
| L4 | Publish | git push, ssh, sudo, chmod | Deny |

## Audit Log

All commands executed through `lockay run` or `lockay shell` are logged to `.lockay/audit.log`:

```
2026-01-01T12:00:00 | ALLOW | risk=L0 | action=executed | exit=0 | pytest tests/
2026-01-01T12:00:05 | ASK   | risk=L3 | action=denied by user | exit=-1 | rm -rf build/
```

## Security Model

The recommended deployment for production agent environments:

```bash
# Create a dedicated owner for protected files
sudo useradd codeguard
sudo chown -R codeguard:codeguard /path/to/repo
sudo chmod -R 755 /path/to/repo

# Agent user can read but not write
# All writes must go through lockay
```

For single-user local development, lockay works without privilege separation -- it enforces policy at the application level.

### Trusted Core

The lock validation and atomic write path is implemented in a minimal C module (`apply.c`, `validate.c`) that:
- Uses only POSIX system calls (openat, read, write, fsync, rename)
- Never calls system(), popen(), or exec()
- Never interprets shell commands
- Validates all edits before touching disk

## Compatibility

- Linux: full support (primary target)
- macOS: should work (uses POSIX APIs only; tested on macOS 12+)
- BSD: should work (POSIX termios + ANSI)
- WSL: works
- Requires: gcc or clang, GNU make

The TUI requires a terminal with ANSI escape code support (xterm, gnome-terminal, iTerm2, Windows Terminal, tmux, screen).

## Project Structure

```
lockay/
  src/
    main.c          CLI dispatch
    tui.c / tui.h   nano-style terminal editor
    filebuf.c / .h  line buffer with char-level editing
    lockdb.c / .h   lock metadata store
    validate.c / .h content-hash lock validation
    apply.c / .h    secure write path
    cmdlock.c / .h  command execution gate
    sha256.c / .h   embedded SHA-256
  tests/
    test_runner.c   51 test cases
  Makefile
  README.md
```

## License

MIT
