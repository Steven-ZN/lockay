<p align="center">
  <br>
  <pre style="font-size: 12px; line-height: 1.2;">
  ▄█       ▄████▄   ▄█  ██▄   ▄█   ▄███▄     ▀▄    ▄  
  ██      ██▀  ▀██  ██  █ █▀  ██   █▀   ▀     █▀▄  ▀  
  ██      ██    ██  ██  █▀█▄  ██   ██          ▀▄▀▄   
  ██      ██    ██  ██  █  █▄ ██   ██▄         ▄  ▄▀  
  ██▄▄▄▄▄ ▀██  ▄█▀  ██  █   █ ██   ▀██▄       ▀▄▀    
                            ▀▀                           v0.1.0
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
  <a href="#english">[ English ]</a> &nbsp;|&nbsp;
  <a href="#chinese">[ Chinese ]</a>
</p>

---

<span id="english"></span>

## English

### What it does

```
                    +--------------+
                    |   LLM Agent   |
                    +------+-------+
                           |
                           |  agent has NO direct write access
                           |  agent has NO unrestricted shell
                           v
                    +--------------+
                    |    lockay      |
                    | capability sh  |
                    +------+-------+
                           |
            +--------------+--------------+
            |                             |
            v                             v
     +--------------+              +--------------+
     |   linelock    |              |   cmdlock     |
     | file mutation |              | command gate  |
     +------+-------+              +------+-------+
            |                             |
            v                             v
     protected files               privileged actions

Agent can read files. Agent cannot write files directly.
Agent can propose edits. lockay validates them.
Agent can propose commands. cmdlock gates them.
```

Two enforcement layers, one binary:

| Layer | Problem | Solution |
|-------|---------|----------|
| **linelock** | Agent rewrites wrong code | Lock file regions by content hash |
| **cmdlock** | Agent runs destructive commands | Policy gate with risk levels L0-L4 |

---

### Install

```bash
# One command:
curl -fsSL https://raw.githubusercontent.com/Steven-ZN/lockay/main/install.sh | sh

# Or from source:
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

# Lock your public API
lockay lock src/api.py 40 80 you "public contract"

# Lock your database schema
lockay lock src/schema.sql 1 50 you "schema v2"

# Show locks
lockay status

# Edit with lock enforcement (locked lines show a visual marker)
lockay edit src/model.py

# Run commands through the gate
lockay run "pytest tests/"
lockay run "rm -rf build/"          # prompts for approval
lockay run "git push origin main"   # denied by default
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

<span id="chinese"></span>

---

## Chinese

### 功能概述

lockay 是 agent 时代的权限层。它管控两个维度：

```
                    +--------------+
                    |   LLM Agent   |
                    +------+-------+
                           |
                           | agent  直接写权限
                           | agent  自由 shell
                           v
                    +--------------+
                    |    lockay      |
                    |  能力受控 shell  |
                    +------+-------+
                           |
            +--------------+--------------+
            |                             |
            v                             v
     +--------------+              +--------------+
     |   linelock    |              |   cmdlock     |
     |  文件修改控制   |              |  命令执行门控   |
     +------+-------+              +------+-------+
            |                             |
            v                             v
      受保护的文件                     受控的操作权限
```

| 层级 | 解决的问题 | 方式 |
|------|-----------|------|
| **linelock** | agent 改错代码 | 基于内容哈希的文件区域锁 |
| **cmdlock** | agent 执行危险命令 | L0-L4 风险分级 + 策略引擎 |

---

### 安装

```bash
# 一键安装:
curl -fsSL https://raw.githubusercontent.com/Steven-ZN/lockay/main/install.sh | sh

# 或从源码编译:
git clone https://github.com/Steven-ZN/lockay.git
cd lockay && make
sudo make install          # 系统级安装
make install-local          # 用户级安装 (~/.local/bin)
```

依赖: gcc 或 clang, GNU make, Linux/macOS/BSD/WSL。零第三方库依赖。

---

### 60 秒上手

```bash
cd /your/project

# 生成命令策略
lockay policy

# 锁定公开 API
lockay lock src/api.py 40 80 steven "public contract"

# 锁定数据库 schema
lockay lock src/schema.sql 1 50 steven "schema v2"

# 查看锁定状态
lockay status

# 用 TUI 编辑器编辑 (锁住的行有 visual marker)
lockay edit src/model.py

# 通过策略门控执行命令
lockay run "pytest tests/"
lockay run "rm -rf build/"          # 弹出审批
lockay run "git push origin main"   # 默认拒绝
```

---

### linelock 原理

锁数据存储在 `.linelock/locks` (纯文本, 可 git 追踪):

```
src/api.py|a1b2c3|40|80|sha256:abc123...|sha256:def456...|sha256:ghi789...|steven|2026-06-07|public contract
```

每条锁记录三种 SHA-256 哈希:

| 哈希 | 内容 |
|------|------|
| **content_hash** | 被锁行内容的 SHA-256 |
| **before_hash** | 锁区前 3 行的 SHA-256 |
| **after_hash** | 锁区后 3 行的 SHA-256 |

当文件其他部分增删导致行号漂移时, lockay 通过在全文件中搜索匹配的内容哈希来自动重锚定锁区。锁保护的是**内容**, 不是**位置**。

```
编辑前:                         编辑后:
  1: import sys                  1: import sys
  2: import os                   2: import os       (新增)
  3:                             3: import re       (新增)
  4: [锁定] def api():           4:
  5: [锁定]     return 42        5: [锁定] def api():    <-- 自动重锚定
  6: [锁定]                     6: [锁定]     return 42
                                 7: [锁定]
```

---

### cmdlock 原理

命令在执行时被分为 5 个风险等级:

```
   L0  安全 ...... ls, cat, grep, echo, find, wc, diff
   L1  执行 ...... python, make, gcc, node, pytest
   L2  写入 ...... touch, mkdir, cp, mv, git add
   L3  危险 ...... rm, pip install, curl, git commit
   L4  发布 ...... git push, ssh, sudo, chmod, kill
```

策略规则 (`.lockay/policy`):

```
ls *              allow      # L0: 始终允许
pytest *          allow      # L1: 始终允许
pip install *     ask        # L3: 询问用户
rm -rf *          ask        # L3: 询问用户
git push *        deny       # L4: 始终拒绝
ssh *             deny       # L4: 始终拒绝
sudo *            deny       # L4: 始终拒绝
```

当命令命中 `ask` 规则时, lockay 弹出交互式审批:

```
--- lockay: command gate ---
Command : rm -rf ./checkpoints
Risk    : L3 (DESTRUCT)
Policy  : ASK
-----------------------------
This command requires approval.
Options: [A]llow once  [D]eny
```

所有决策被记录到 `.lockay/audit.log`。

---

### Agent 接口

为 LLM tool-calling 设计, 结构化退出码:

```bash
lockay show   src/main.c 100 180           # 带锁标注查看
lockay set    src/main.c 150 "new text"    # 修改一行
lockay insert src/main.c 100 "new line"    # 在某行前插入
lockay delete src/main.c 100 105           # 删除行范围
lockay apply  src/main.c /tmp/patch.diff   # 应用 unified diff
lockay run    "pytest tests/"              # 通过策略门执行
```

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 拒绝 (编辑越界或策略拦截) |
| 2 | 错误 (文件不存在等) |

---

### 安全模型

```
  .---------------.         .---------------.
  |   agent 用户    |         |  codeguard     |
  |   (只读)       |         |  (拥有文件)     |
  '-------^-------'         '-------^-------'
          |                          |
          |   agent 调用              |   lockay 验证
          |   lockay CLI              |   后执行写入
          |                          |
  .-------'--------------------------'-------.
  |              lockay binary                |
  |                                           |
  |   读文件 -> 验证锁区 -> 原子写入            |
  |   (临时文件 + fsync + rename)              |
  |                                           |
  |   可信核心决不调用:                          |
  |     system() popen() exec() shell()        |
  '-------------------------------------------'
```

生产环境: `chown codeguard:codeguard repo/` + `chmod 755 repo/`。agent 以不同用户运行, 只有读权限, 所有写入经 lockay。

本地开发: lockay 在应用层执行策略, 不需要权限分离。

---

### 项目结构

```
lockay/
  src/
    main.c          CLI 分发 (14 个子命令)
    tui.c           nano 风格终端编辑器
    filebuf.c       行缓冲区 + 字符级编辑
    lockdb.c        锁元数据存储
    validate.c      内容哈希验证 + 重锚定
    apply.c         安全写入路径 (原子写入)
    cmdlock.c       命令门控 (策略引擎 + 审计)
    sha256.c        内嵌 SHA-256 (公共领域)
  tests/
    test_runner.c   51 个测试用例
  install.sh        交互式安装脚本 (中/英)
  Makefile
```

---

<p align="center">
  <b>Lockay</b> &mdash; A simple, auditable, zero-dependency Linux tool.<br>
  Not a platform. Not a service. Just a binary that enforces boundaries.<br>
  <br>
  <sub>MIT License &nbsp;|&nbsp; <a href="https://github.com/Steven-ZN/lockay">github.com/Steven-ZN/lockay</a></sub>
</p>
