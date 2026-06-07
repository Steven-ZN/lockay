<p align="center">
  <br>
  <pre style="font-size: 12px; line-height: 1.2;">
░██           ░██████     ░██████  ░██     ░██    ░███    ░██     ░██ 
░██          ░██   ░██   ░██   ░██ ░██    ░██    ░██░██    ░██   ░██  
░██         ░██     ░██ ░██        ░██   ░██    ░██  ░██    ░██ ░██   
░██         ░██     ░██ ░██        ░███████    ░█████████    ░████    
░██         ░██     ░██ ░██        ░██   ░██   ░██    ░██     ░██     
░██          ░██   ░██   ░██   ░██ ░██    ░██  ░██    ░██     ░██     
░██████████   ░██████     ░██████  ░██     ░██ ░██    ░██     ░██     v0.1.0
                                                                      
                                                                      
                                                                         
  </pre>
  <br>
  <i>chmod for lines of code &nbsp;|&nbsp; sudo for autonomous agents</i>
  <br>
  <br>
</p>

---

<p align="center">
  <b>lockay</b> is a capability layer for the agent era.<br>
  It controls <b>what code an agent can change</b> and <b>what commands an agent can run</b>.<br>
  No FUSE. No kernel modules. Just a small, auditable C binary.
</p>

<p align="center">
  <a href="README_CN.md">[ Chinese / 中文 ]</a>
</p>

---

### What it does

<p align="center">
  <img src="info.png" alt="lockay architecture" width="600">
</p>

Agent can read files. Agent cannot write files directly.
Agent can propose edits. lockay validates them.
Agent can propose commands. cmdlock gates them.

Two enforcement layers, one binary:

| Layer | Problem | Solution |
|-------|---------|----------|
| **linelock** | Agent rewrites wrong code | Lock file regions by content hash |
| **cmdlock** | Agent runs destructive commands | Policy gate with risk levels L0-L4 |

---

### Install

**Use the install script (recommended):**

```bash
curl -fsSL https://raw.githubusercontent.com/Steven-ZN/lockay/main/install.sh | sh
```

**Or build from source:**

```bash
git clone https://github.com/Steven-ZN/lockay.git
cd lockay && make
sudo make install          # system-wide
make install-local          # user-local (~/.local/bin)
```

Requirements: gcc or clang, GNU make, Linux/macOS/BSD/WSL. Zero library dependencies.

---

### Quick start (60 seconds)

```bash
cd /your/project

# Generate command policy
lockay policy

# Lock a range of lines by line number
lockay lock src/api.py 40 80 you "public contract"
# Format: lockay lock <file> <start_line> <end_line> [owner] [reason]

# View what is locked
lockay status                  # all files
lockay status src/api.py       # one file

# Show file with lock marks (locked lines are marked)
lockay show src/api.py

# Unlock by lock ID (the 6-char ID shown in status)
lockay unlock a1b2c3

# Edit with lock enforcement
lockay edit src/model.py

# Run commands through the gate
lockay run "pytest tests/"
lockay run "rm -rf build/"          # prompts for approval
lockay run "git push origin main"   # denied by default
```

Lock vs unlock at a glance:

| Action | Command | Notes |
|--------|---------|-------|
| Lock lines | `lockay lock <file> <start> <end> [owner] [reason]` | Returns a 6-char lock ID |
| See locks | `lockay status [file]` | Lists all locks with their IDs |
| See content | `lockay show <file>` | Locked lines show visual markers |
| Unlock | `lockay unlock <lock_id>` | Use the ID from `status` |
| Verify | `lockay check <file>` | Checks no lock content has been modified |
```

---

### How linelock works

Locks are stored in `.linelock/locks` (plain text, git-trackable):

```
src/api.py|a1b2c3|40|80|sha256:abc123...|sha256:def456...|sha256:ghi789...|you|2026-06-07|public contract
```

Each lock records three SHA-256 hashes:

| Hash | Content |
|------|---------|
| **content_hash** | Locked region content |
| **before_hash** | 3 lines before the lock |
| **after_hash** | 3 lines after the lock |

When line numbers shift (inserts/deletes elsewhere in the file), lockay re-anchors the lock by finding its content hash. The lock protects **content**, not position.

```
Before edit:                    After edit:
  1: import sys                  1: import sys
  2: import os                   2: import os       (new)
  3:                             3: import re       (new)
  4: [LOCKED] def api():         4:
  5: [LOCKED]     return 42      5: [LOCKED] def api():    <-- re-anchored
  6: [LOCKED]                    6: [LOCKED]     return 42
                                 7: [LOCKED]
```

---

### How cmdlock works

Commands are classified into 5 risk levels:

```
   L0  SAFE ........ ls, cat, grep, echo, find, wc, diff
   L1  EXEC ........ python, make, gcc, node, pytest
   L2  WRITE ....... touch, mkdir, cp, mv, git add
   L3  DESTRUCT .... rm, pip install, curl, git commit
   L4  PUBLISH ..... git push, ssh, sudo, chmod, kill
```

Policy rules in `.lockay/policy`:

```
ls *              allow      # L0: always safe
pytest *          allow      # L1: always safe
pip install *     ask        # L3: ask the user
rm -rf *          ask        # L3: ask the user
git push *        deny       # L4: never allowed
ssh *             deny       # L4: never allowed
sudo *            deny       # L4: never allowed
```

When a command hits an `ask` rule, lockay prompts interactively:

```
--- lockay: command gate ---
Command : rm -rf ./checkpoints
Risk    : L3 (DESTRUCT)
Policy  : ASK
-----------------------------
This command requires approval.
Options: [A]llow once  [D]eny
```

All decisions are written to `.lockay/audit.log`.

---

### Agent-facing CLI

Designed for LLM tool-calling. Structured exit codes:

```bash
lockay show   src/main.c 100 180           # view with lock annotations
lockay set    src/main.c 150 "new text"    # change a line
lockay insert src/main.c 100 "new line"    # insert before line
lockay delete src/main.c 100 105           # delete range
lockay apply  src/main.c /tmp/patch.diff   # apply unified diff
lockay run    "pytest tests/"              # run through policy gate
```

| Exit code | Meaning |
|-----------|---------|
| 0 | Success |
| 1 | Denied (outside editable region, or blocked by policy) |
| 2 | Error (file not found, parse error) |

---

### Security model

```
  .---------------.         .---------------.
  |   agent user   |         |  codeguard     |
  |  (read only)   |         |  (owns files)  |
  '-------^-------'         '-------^-------'
          |                          |
          |   agent invokes          |   lockay validates
          |   lockay CLI             |   then writes
          |                          |
  .-------'--------------------------'-------.
  |              lockay binary                |
  |                                           |
  |   load file -> validate locks -> atomic   |
  |   write (temp + fsync + rename)           |
  |                                           |
  |   Trusted core never calls:               |
  |     system() popen() exec() shell()       |
  '-------------------------------------------'
```

For production: `chown codeguard:codeguard repo/` + `chmod 755 repo/`. Agents run as a different user with read-only access. All writes go through lockay.

For local dev: lockay enforces policy at the application level. No privilege separation needed.

---

### Project layout

```
lockay/
  src/
    main.c          CLI dispatch (14 subcommands)
    tui.c           nano-style terminal editor
    filebuf.c       line buffer + char-level editing
    lockdb.c        lock metadata store
    validate.c      content-hash validation + re-anchoring
    apply.c         secure write path (atomic)
    cmdlock.c       command gate (policy engine + audit)
    sha256.c        embedded SHA-256 (public domain)
  tests/
    test_runner.c   51 test cases
  install.sh        interactive installer (EN/ZH)
  Makefile
```

---

<p align="center">
  <b>lockay</b> &mdash; A simple, auditable, zero-dependency Linux tool.<br>
  Not a platform. Not a service. Just a binary that enforces boundaries.<br>
  <br>
  <sub>MIT License &nbsp;|&nbsp; <a href="https://github.com/Steven-ZN/lockay">github.com/Steven-ZN/lockay</a></sub>
</p>
