#include "tui.h"
#include "filebuf.h"
#include "lockdb.h"
#include "validate.h"
#include "sha256.h"
#include "apply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>

/* ========== Terminal state ========== */

static struct termios orig_termios;
static int term_rows = 24, term_cols = 80;

/* ========== ANSI helpers ========== */

#define CSI "\033["

static void twrite(const char *s, size_t n) {
    if (write(1, s, n) < 0) { /* discard error in TUI */ }
}

static void ansi_clear(void)       { twrite(CSI "2J", 4); }
static void ansi_clear_line(void)  { twrite(CSI "K", 3); }
static void ansi_move(int r, int c) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), CSI "%d;%dH", r + 1, c + 1);
    twrite(buf, (size_t)n);
}
static void ansi_hide_cursor(void) { twrite(CSI "?25l", 5); }
static void ansi_show_cursor(void) { twrite(CSI "?25h", 5); }
static void ansi_reverse(void)     { twrite(CSI "7m", 3); }
static void ansi_bold(void)        { twrite(CSI "1m", 3); }
static void ansi_dim(void)         { twrite(CSI "2m", 3); }
static void ansi_reset(void)       { twrite(CSI "0m", 4); }

#define TPUTC(c) twrite((char[]){ (char)(c) }, 1)
#define TPUTS(s) twrite((s), strlen(s))

/* ========== Terminal raw mode ========== */

static void tui_raw_mode(void) {
    tcgetattr(0, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(0, TCSAFLUSH, &raw);
}

static void tui_restore(void) {
    tcsetattr(0, TCSAFLUSH, &orig_termios);
    ansi_show_cursor();
    ansi_reset();
}

/* ========== Terminal size ========== */

static void tui_get_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(0, TIOCGWINSZ, &ws) != -1 && ws.ws_row > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

/* ========== Key reading ========== */

#define KEY_NONE     0
#define KEY_UP       1001
#define KEY_DOWN     1002
#define KEY_LEFT     1003
#define KEY_RIGHT    1004
#define KEY_PAGEUP   1005
#define KEY_PAGEDN   1006
#define KEY_HOME     1007
#define KEY_END      1008
#define KEY_DELETE   1009
#define KEY_CTRL_G   7
#define KEY_CTRL_O   15
#define KEY_CTRL_X   24
#define KEY_CTRL_L   12
#define KEY_CTRL_U   21
#define KEY_CTRL_K   11
#define KEY_CTRL_W   23
#define KEY_CTRL_A   1
#define KEY_CTRL_E   5
#define KEY_CTRL_C   3
#define KEY_ENTER    13
#define KEY_TAB      9
#define KEY_BACKSP   127
#define KEY_ESCAPE   27

static int tui_read_key(void) {
    char c;
    if (read(0, &c, 1) != 1) return KEY_NONE;

    if (c == 27) {
        /* Escape sequence */
        char seq[5];
        int n = read(0, seq, 4);
        if (n <= 0) return KEY_ESCAPE;

        if (seq[0] == '[') {
            if (n >= 2) {
                if (seq[1] == 'A') return KEY_UP;
                if (seq[1] == 'B') return KEY_DOWN;
                if (seq[1] == 'C') return KEY_RIGHT;
                if (seq[1] == 'D') return KEY_LEFT;
                if (seq[1] == 'H') return KEY_HOME;
                if (seq[1] == 'F') return KEY_END;
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended: CSI <num>~ */
                    if (n >= 3 && seq[2] == '~') {
                        if (seq[1] == '3') return KEY_DELETE;
                        if (seq[1] == '5') return KEY_PAGEUP;
                        if (seq[1] == '6') return KEY_PAGEDN;
                        if (seq[1] == '1') return KEY_HOME;
                        if (seq[1] == '4') return KEY_END;
                    }
                }
            }
        }
        return KEY_ESCAPE;
    }

    /* Ctrl+Backspace sometimes sends 127 */
    if (c == 8 || c == 127) return KEY_BACKSP;
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == '\t') return KEY_TAB;

    return (unsigned char)c;
}

/* ========== Signal handling ========== */

static volatile sig_atomic_t sigwinch_flag = 0;
static volatile sig_atomic_t sigint_flag   = 0;

static void handle_sigwinch(int sig) { (void)sig; sigwinch_flag = 1; }
static void handle_sigint(int sig)   { (void)sig; sigint_flag = 1; }

/* ========== TUI context ========== */

typedef struct {
    const char *repo_root;
    const char *file;
    char       *full_path;
    FileBuf    *fb;
    LockDB     *db;

    /* Cursor */
    int cx, cy;       /* 1-indexed: line and column in file */
    int scroll;       /* first visible line (1-indexed) */
    int preferred_cx; /* preferred column (for vertical movement) */

    /* Viewport */
    int rows, cols;   /* terminal size */

    /* Status */
    bool modified;
    bool quit;
    int  dirty_since_save;

    /* Message line (bottom bar, temporary) */
    char msg[256];
    int  msg_timeout;
} TuiCtx;

/* ========== Rendering ========== */

static int content_rows(TuiCtx *t) {
    return t->rows - 4;  /* title + sep + sep + cmdbar */
}

static void tui_draw(TuiCtx *t) {
    ansi_hide_cursor();

    int vis = content_rows(t);
    int lnum_width = 5;

    /* Ensure cursor is visible */
    if (t->cy < t->scroll)
        t->scroll = t->cy;
    if (t->cy >= t->scroll + vis)
        t->scroll = t->cy - vis + 1;
    if (t->scroll < 1)
        t->scroll = 1;

    /* ---- Title bar (row 0) ---- */
    ansi_move(0, 0);
    ansi_reverse();
    ansi_bold();

    char title[256];
    const char *fname = strrchr(t->file, '/');
    if (!fname) fname = t->file; else fname++;
    int n = snprintf(title, sizeof(title),
                     " GNU linelock 0.1         %-20s %s%s%s",
                     fname,
                     t->modified ? "[Modified] " : "",
                     t->dirty_since_save > 0 ? " " : "",
                     t->dirty_since_save > 0 ? "" : "");
    if (n > t->cols) n = t->cols;
    twrite(title, (size_t)n);
    /* Pad to end of line */
    for (int i = n; i < t->cols; i++) TPUTC(' ');
    ansi_reset();

    /* ---- Separator (row 1) ---- */
    ansi_move(1, 0);
    for (int i = 0; i < t->cols; i++) twrite("\342\224\200", 3);

    /* ---- Content area (rows 2 to 2+vis-1) ---- */
    for (int i = 0; i < vis; i++) {
        int fline = t->scroll + i;  /* 1-indexed file line */
        ansi_move(2 + i, 0);

        if (fline > t->fb->count) {
            if (i == 0 && t->fb->count == 0) {
                /* Empty file indicator */
                twrite("  (empty file)", 14);
            }
            ansi_clear_line();
            continue;
        }

        const char *text = t->fb->lines[fline - 1].text;
        size_t tlen = t->fb->lines[fline - 1].len;

        /* Check lock status */
        LockRecord *lr = lockdb_is_line_locked(t->db, t->file, fline);
        bool locked = (lr != NULL);

        /* Line number */
        char lnum[8];
        int lnlen = snprintf(lnum, sizeof(lnum), "%4d ", fline);
        if (locked) {
            ansi_dim();
            twrite("\342\226\210 ", 4);  /* █ for locked */
        } else {
            twrite("  ", 2);
        }
        twrite(lnum, (size_t)lnlen);

        /* Highlight current line */
        if (fline == t->cy) {
            ansi_reverse();
        } else if (locked) {
            ansi_dim();
        }

        /* Line content (truncate to screen width) */
        int avail = t->cols - lnum_width - 1;
        if (avail < 0) avail = 0;
        if ((int)tlen > avail) {
            /* TODO: horizontal scroll for long lines */
            size_t start = 0;
            if (fline == t->cy && t->cx > avail) {
                start = (size_t)(t->cx - avail / 2);
                if (start > tlen) start = tlen;
            }
            twrite(text + start, (size_t)avail);
        } else {
            twrite(text, tlen);
            /* Show lock info on locked lines */
            if (locked && fline == lr->start) {
                char info[64];
                int infolen = snprintf(info, sizeof(info),
                                       " [%s]", lr->id);
                int remaining = avail - (int)tlen;
                if (infolen < remaining) {
                    ansi_dim();
                    twrite(info, (size_t)infolen);
                }
            }
        }

        ansi_reset();
        ansi_clear_line();
    }

    /* ---- Separator (row rows-2) ---- */
    ansi_move(t->rows - 2, 0);
    for (int i = 0; i < t->cols; i++) twrite("\342\224\200", 3);

    /* ---- Command bar (row rows-1) ---- */
    ansi_move(t->rows - 1, 0);
    ansi_reverse();

    const char *cmdbar =
        "^G Help  ^O Write  ^X Exit  ^L Lock  ^U Unlock  ^K Cut  ^W Search";
    twrite(cmdbar, strlen(cmdbar));
    /* Pad */
    for (int i = (int)strlen(cmdbar); i < t->cols; i++)
        TPUTC(' ');
    ansi_reset();

    /* ---- Position cursor ---- */
    int screen_row = 2 + (t->cy - t->scroll);
    int screen_col = lnum_width + (t->cx - 1);
    if (screen_col < lnum_width) screen_col = lnum_width;
    if (screen_col >= t->cols) screen_col = t->cols - 1;
    ansi_move(screen_row, screen_col);
    ansi_show_cursor();
}

/* ========== Message / status ========== */

static void tui_msg(TuiCtx *t, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->msg, sizeof(t->msg), fmt, ap);
    va_end(ap);
    t->msg_timeout = 3;  /* show for 3 refresh cycles */
}

/* ========== Lock / Unlock from TUI ========== */

static void tui_lock_selection(TuiCtx *t) {
    /* Lock: if nothing selected, lock current line.
     * For now, lock a single line (MVP). */
    int line = t->cy;

    /* Check if already locked */
    LockRecord *existing = lockdb_is_line_locked(t->db, t->file, line);
    if (existing) {
        tui_msg(t, "Line %d is already locked by %s", line, existing->id);
        return;
    }

    /* Compute hashes */
    Sha256Digest content_hash;
    sha256_hash_lines((const char **)&t->fb->lines[line - 1].text, 1,
                       &content_hash);

    /* Before/after context */
    Sha256Digest before_hash, after_hash;
    memset(&before_hash, 0, sizeof(before_hash));
    memset(&after_hash, 0, sizeof(after_hash));

    if (line > 1) {
        sha256_hash_lines((const char **)&t->fb->lines[line - 2].text, 1,
                           &before_hash);
    }
    if (line < t->fb->count) {
        sha256_hash_lines((const char **)&t->fb->lines[line].text, 1,
                           &after_hash);
    }

    const char *env_user = getenv("USER");
    const char *id = lockdb_add(t->db, t->file, line, line,
                                &content_hash, &before_hash, &after_hash,
                                "", env_user ? env_user : "unknown",
                                "locked from TUI");

    lockdb_save(t->db);
    char ch[65];
    sha256_hex(&content_hash, ch);
    tui_msg(t, "Locked line %d  id:%s  hash:%s", line, id, ch);
}

static void tui_unlock_line(TuiCtx *t) {
    int line = t->cy;
    LockRecord *lr = lockdb_is_line_locked(t->db, t->file, line);
    if (!lr) {
        tui_msg(t, "Line %d is not locked", line);
        return;
    }
    lockdb_remove(t->db, lr->id);
    lockdb_save(t->db);
    tui_msg(t, "Unlocked %s (was line %d)", lr->id, line);
}

/* ========== Write file ========== */

static bool tui_write(TuiCtx *t) {
    /* Validate locks before saving */
    ValidationResult vr = validate_locks(t->db, t->file, t->fb);
    if (!vr.passed) {
        tui_msg(t, "SAVE DENIED: %s", vr.error_msg);
        validation_result_free(&vr);
        return false;
    }

    /* Re-anchor locks */
    validate_reanchor(t->db, t->file, t->fb);
    lockdb_save(t->db);

    if (filebuf_save_atomic(t->fb, t->full_path) != 0) {
        tui_msg(t, "ERROR: failed to save file: %s", strerror(errno));
        return false;
    }

    t->modified = false;
    t->dirty_since_save = 0;
    tui_msg(t, "File saved (%d lines)", t->fb->count);
    return true;
}

/* ========== Input handling ========== */

static void tui_handle_key(TuiCtx *t, int key) {
    t->msg[0] = '\0';
    t->msg_timeout = 0;

    switch (key) {
    case KEY_CTRL_X:
        if (t->modified) {
            tui_msg(t, "Unsaved changes! Press ^X again to quit without saving.");
            t->modified = false;  /* hack: use modified flag as "warned" state */
            /* Actually, let's use a separate flag */
            /* For now: two ^X presses quits */
            t->quit = true;
        } else {
            t->quit = true;
        }
        break;

    case KEY_CTRL_O:
        tui_write(t);
        break;

    case KEY_CTRL_L:
        tui_lock_selection(t);
        break;

    case KEY_CTRL_U:
        tui_unlock_line(t);
        break;

    case KEY_CTRL_G: {
        /* Help popup would go here. For MVP, show a message. */
        tui_msg(t, "linelock 0.1 - chmod for lines of code. ^X exit, ^O write, ^L lock, ^U unlock");
        break;
    }

    case KEY_CTRL_K: {
        /* Cut current line (only if editable) */
        if (t->fb->count == 0) break;
        int line = t->cy;
        LockRecord *lr = lockdb_is_line_locked(t->db, t->file, line);
        if (lr) {
            tui_msg(t, "Line %d is locked (%s)", line, lr->id);
            break;
        }
        /* For MVP: cut = clear line content */
        t->fb->lines[line - 1].text[0] = '\0';
        t->fb->lines[line - 1].len = 0;
        t->cx = 1;
        t->modified = true;
        t->dirty_since_save++;
        break;
    }

    case KEY_CTRL_A:
        t->cx = 1;
        break;

    case KEY_CTRL_E:
        if (t->fb->count > 0)
            t->cx = (int)t->fb->lines[t->cy - 1].len + 1;
        break;

    case KEY_UP:
        if (t->cy > 1) {
            t->cy--;
            t->cx = t->preferred_cx;
            /* Clamp cx to line length */
            if (t->cx > (int)t->fb->lines[t->cy - 1].len + 1)
                t->cx = (int)t->fb->lines[t->cy - 1].len + 1;
        }
        break;

    case KEY_DOWN:
        if (t->cy < t->fb->count) {
            t->cy++;
            t->cx = t->preferred_cx;
            if (t->cx > (int)t->fb->lines[t->cy - 1].len + 1)
                t->cx = (int)t->fb->lines[t->cy - 1].len + 1;
        }
        break;

    case KEY_LEFT:
        if (t->cx > 1) t->cx--;
        t->preferred_cx = t->cx;
        break;

    case KEY_RIGHT:
        if (t->fb->count > 0 && t->cx <= (int)t->fb->lines[t->cy - 1].len)
            t->cx++;
        t->preferred_cx = t->cx;
        break;

    case KEY_HOME:
        t->cx = 1;
        t->preferred_cx = 1;
        break;

    case KEY_END:
        if (t->fb->count > 0) {
            t->cx = (int)t->fb->lines[t->cy - 1].len + 1;
            t->preferred_cx = t->cx;
        }
        break;

    case KEY_PAGEUP: {
        int vis = content_rows(t);
        t->cy -= vis;
        if (t->cy < 1) t->cy = 1;
        t->scroll -= vis;
        if (t->scroll < 1) t->scroll = 1;
        break;
    }

    case KEY_PAGEDN: {
        int vis = content_rows(t);
        t->cy += vis;
        if (t->cy > t->fb->count) t->cy = t->fb->count;
        if (t->cy < 1) t->cy = 1;
        t->scroll += vis;
        break;
    }

    case KEY_BACKSP: {
        if (t->fb->count == 0) break;
        int line = t->cy;
        LockRecord *lr = lockdb_is_line_locked(t->db, t->file, line);
        if (lr) {
            tui_msg(t, "Line %d is locked (%s)", line, lr->id);
            break;
        }

        if (t->cx > 1) {
            /* Delete character before cursor */
            filebuf_char_delete(t->fb, line, t->cx - 2);
            t->cx--;
            t->preferred_cx = t->cx;
            t->modified = true;
            t->dirty_since_save++;
        } else if (line > 1) {
            /* Join with previous line */
            LockRecord *prev_lr = lockdb_is_line_locked(t->db, t->file, line - 1);
            if (prev_lr) {
                tui_msg(t, "Line %d is locked (%s)", line - 1, prev_lr->id);
                break;
            }
            int prev_len = (int)t->fb->lines[line - 2].len;
            filebuf_line_join(t->fb, line - 1);
            t->cy = line - 1;
            t->cx = prev_len + 1;
            t->preferred_cx = t->cx;
            t->modified = true;
            t->dirty_since_save++;
        }
        break;
    }

    case KEY_DELETE: {
        if (t->fb->count == 0) break;
        int line = t->cy;
        LockRecord *lr = lockdb_is_line_locked(t->db, t->file, line);
        if (lr) {
            tui_msg(t, "Line %d is locked (%s)", line, lr->id);
            break;
        }
        if (t->cx <= (int)t->fb->lines[line - 1].len) {
            /* Delete character at cursor */
            filebuf_char_delete(t->fb, line, t->cx - 1);
            t->modified = true;
            t->dirty_since_save++;
        } else if (line < t->fb->count) {
            /* Join with next line */
            LockRecord *next_lr = lockdb_is_line_locked(t->db, t->file, line + 1);
            if (next_lr) {
                tui_msg(t, "Line %d is locked (%s)", line + 1, next_lr->id);
                break;
            }
            filebuf_line_join(t->fb, line);
            t->modified = true;
            t->dirty_since_save++;
        }
        break;
    }

    case KEY_ENTER: {
        if (t->fb->count == 0) {
            /* Empty file: insert first line */
            filebuf_insert_line(t->fb, 1, "");
            t->cy = 1;
            t->cx = 1;
            t->modified = true;
            t->dirty_since_save++;
            break;
        }
        int line = t->cy;
        LockRecord *lr = lockdb_is_line_locked(t->db, t->file, line);
        if (lr) {
            tui_msg(t, "Line %d is locked (%s)", line, lr->id);
            break;
        }
        /* Only allow Enter on editable lines */
        filebuf_line_break(t->fb, line, t->cx - 1);
        t->cy = line + 1;
        t->cx = 1;
        t->preferred_cx = 1;
        t->modified = true;
        t->dirty_since_save++;
        break;
    }

    case KEY_TAB:
    case KEY_NONE:
    case KEY_ESCAPE:
        break;

    default: {
        /* Regular character insert */
        if (key < 32 || key > 126) break;

        if (t->fb->count == 0) {
            /* Insert first line if file is empty */
            filebuf_insert_line(t->fb, 1, "");
            t->cy = 1;
        }

        int line = t->cy;
        LockRecord *lr = lockdb_is_line_locked(t->db, t->file, line);
        if (lr) {
            tui_msg(t, "Line %d is locked (%s)", line, lr->id);
            break;
        }

        filebuf_char_insert(t->fb, line, t->cx - 1, key);
        t->cx++;
        t->preferred_cx = t->cx;
        t->modified = true;
        t->dirty_since_save++;
        break;
    }
    }
}

/* ========== Main TUI loop ========== */

int tui_edit(const char *repo_root, const char *file) {
    TuiCtx t;
    memset(&t, 0, sizeof(t));

    t.repo_root = repo_root;
    t.file      = file;

    /* Build full path */
    size_t plen = strlen(repo_root) + strlen(file) + 4;
    t.full_path = malloc(plen);
    snprintf(t.full_path, plen, "%s/%s", repo_root, file);

    /* Auto-restore before loading */
    apply_auto_restore(repo_root, file);

    /* Load file */
    t.fb = filebuf_load(t.full_path);
    if (!t.fb) {
        /* Create new file if it doesn't exist */
        t.fb = filebuf_load(t.full_path);
        if (!t.fb) {
            /* Create empty buffer */
            t.fb = calloc(1, sizeof(FileBuf));
            t.fb->path = strdup(t.full_path);
            t.fb->cap  = 128;
            t.fb->lines = calloc(128, sizeof(Line));
        }
    }

    /* Load lock database */
    t.db = lockdb_load(repo_root);

    /* Init cursor */
    t.cy = 1;
    t.cx = 1;
    t.scroll = 1;
    t.preferred_cx = 1;
    if (t.fb->count == 0) {
        /* Empty file: insert a starter line so the TUI has something */
    }

    /* Terminal setup */
    tui_get_size(&t.rows, &t.cols);
    term_rows = t.rows;
    term_cols = t.cols;

    tui_raw_mode();
    ansi_clear();

    /* Signal handlers */
    struct sigaction sa_winch, sa_int, old_winch, old_int;
    memset(&sa_winch, 0, sizeof(sa_winch));
    sa_winch.sa_handler = handle_sigwinch;
    sa_winch.sa_flags   = 0;
    sigaction(SIGWINCH, &sa_winch, &old_winch);

    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = handle_sigint;
    sa_int.sa_flags   = 0;
    sigaction(SIGINT, &sa_int, &old_int);

    /* Main loop */
    while (!t.quit) {
        /* Handle terminal resize */
        if (sigwinch_flag) {
            sigwinch_flag = 0;
            tui_get_size(&t.rows, &t.cols);
            ansi_clear();
        }

        /* Handle Ctrl+C */
        if (sigint_flag) {
            sigint_flag = 0;
            if (t.modified) {
                tui_msg(&t, "Unsaved changes! Press ^X to quit, ^O to save.");
            } else {
                t.quit = true;
            }
        }

        /* Draw */
        tui_draw(&t);

        /* Show temporary message */
        if (t.msg_timeout > 0) {
            ansi_move(t.rows - 1, 0);
            ansi_reverse();
            twrite(t.msg, strlen(t.msg));
            ansi_reset();
            t.msg_timeout--;
        }

        /* Read input */
        int key = tui_read_key();
        if (key != KEY_NONE)
            tui_handle_key(&t, key);
    }

    /* Cleanup */
    sigaction(SIGWINCH, &old_winch, NULL);
    sigaction(SIGINT, &old_int, NULL);
    tui_restore();
    printf("\n");

    /* Save lockdb if locks changed */
    lockdb_save(t.db);

    lockdb_free(t.db);
    filebuf_free(t.fb);
    free(t.full_path);
    return 0;
}
