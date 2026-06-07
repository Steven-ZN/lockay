#include "apply.h"
#include "tui.h"
#include "cmdlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static void usage(const char *prog) {
    printf("lockay - capability shell for coding agents\n\n");
    printf("Usage: %s <command> [args...]\n\n", prog);
    printf("File lock commands:\n");
    printf("  edit   <file>                  Nano-style editor with lock enforcement\n");
    printf("  show   <file> [start] [end]    Show file with lock annotations\n");
    printf("  lock   <file> <start> <end>    Lock lines [owner] [reason]\n");
    printf("  unlock <lock_id>               Remove a lock\n");
    printf("  status [file]                  Show lock status\n");
    printf("  apply  <file> <patch>          Apply a patch (respects locks)\n");
    printf("  check  <file>                  Verify lock integrity\n");
    printf("  set    <file> <line> <text>    Set a line (agent CLI)\n");
    printf("  insert <file> <line> <text>    Insert before line (agent CLI)\n");
    printf("  delete <file> <start> <end>    Delete lines (agent CLI)\n");
    printf("\nCommand lock commands:\n");
    printf("  run    <command>               Run a command through policy gate\n");
    printf("  shell                          Enter interactive guarded shell\n");
    printf("  policy [file]                  Show or generate default policy\n");
    printf("\nEnvironment:\n");
    printf("  LOCKAY_ROOT   Repository root path (default: current directory)\n");
}

static const char *get_repo_root(void) {
    const char *root = getenv("LOCKAY_ROOT");
    return root ? root : ".";
}

/* Join argv from idx to argc-1 with spaces */
static char *join_args(int argc, char *argv[], int start) {
    size_t len = 0;
    for (int i = start; i < argc; i++)
        len += strlen(argv[i]) + 1;
    if (len == 0) return strdup("");

    char *s = malloc(len);
    if (!s) return NULL;
    s[0] = '\0';
    for (int i = start; i < argc; i++) {
        if (i > start) strcat(s, " ");
        strcat(s, argv[i]);
    }
    return s;
}

static int cmd_edit(int argc, char *argv[], const char *repo) {
    if (argc < 3) { fprintf(stderr, "Usage: lockay edit <file>\n"); return 1; }
    return tui_edit(repo, argv[2]);
}

static int cmd_show(int argc, char *argv[], const char *repo) {
    if (argc < 3) { fprintf(stderr, "Usage: lockay show <file> [start] [end]\n"); return 1; }
    int start = argc > 3 ? atoi(argv[3]) : 1;
    int end   = argc > 4 ? atoi(argv[4]) : -1;
    return apply_show(repo, argv[2], start, end);
}

static int cmd_lock(int argc, char *argv[], const char *repo) {
    if (argc < 5) {
        fprintf(stderr, "Usage: lockay lock <file> <start> <end> [owner] [reason]\n");
        return 1;
    }
    const char *owner  = argc > 5 ? argv[5] : NULL;
    const char *reason = argc > 6 ? argv[6] : NULL;
    return apply_lock(repo, argv[2], atoi(argv[3]), atoi(argv[4]), owner, reason);
}

static int cmd_unlock(int argc, char *argv[], const char *repo) {
    if (argc < 3) { fprintf(stderr, "Usage: lockay unlock <lock_id>\n"); return 1; }
    return apply_unlock(repo, argv[2]);
}

static int cmd_status(int argc, char *argv[], const char *repo) {
    return apply_status(repo, argc > 2 ? argv[2] : NULL);
}

static int cmd_apply(int argc, char *argv[], const char *repo) {
    if (argc < 4) {
        fprintf(stderr, "Usage: lockay apply <file> <patch_file>\n");
        return 1;
    }
    return apply_patch(repo, argv[2], argv[3]);
}

static int cmd_check(int argc, char *argv[], const char *repo) {
    if (argc < 3) { fprintf(stderr, "Usage: lockay check <file>\n"); return 1; }
    return apply_check(repo, argv[2]);
}

static int cmd_set(int argc, char *argv[], const char *repo) {
    if (argc < 5) {
        fprintf(stderr, "Usage: lockay set <file> <line> <text>\n");
        return 1;
    }
    char *text = join_args(argc, argv, 4);
    if (!text) return 2;
    int rc = apply_set_line(repo, argv[2], atoi(argv[3]), text);
    free(text);
    return rc;
}

static int cmd_insert(int argc, char *argv[], const char *repo) {
    if (argc < 5) {
        fprintf(stderr, "Usage: lockay insert <file> <line> <text>\n");
        return 1;
    }
    char *text = join_args(argc, argv, 4);
    if (!text) return 2;
    int rc = apply_insert_line(repo, argv[2], atoi(argv[3]), text);
    free(text);
    return rc;
}

static int cmd_delete(int argc, char *argv[], const char *repo) {
    if (argc < 5) {
        fprintf(stderr, "Usage: lockay delete <file> <start> <end>\n");
        return 1;
    }
    return apply_delete_lines(repo, argv[2], atoi(argv[3]), atoi(argv[4]));
}

static int cmd_run(int argc, char *argv[], const char *repo) {
    if (argc < 3) {
        fprintf(stderr, "Usage: lockay run <command>\n");
        return 1;
    }

    cmdlock_init(repo);

    /* Reconstruct the full command from argv[2..] */
    char *cmd = join_args(argc, argv, 2);
    if (!cmd) { cmdlock_free(); return 2; }

    int rc = cmdlock_run(cmd);
    free(cmd);
    cmdlock_free();
    return rc;
}

static int cmd_shell(const char *repo) {
    printf("lockay guarded shell\n");
    printf("Type 'exit' or Ctrl+D to quit.\n\n");

    cmdlock_init(repo);

    char *line;
    while ((line = cmdlock_readline()) != NULL) {
        if (strcmp(line, "") == 0)  { free(line); continue; }
        if (strcmp(line, "exit") == 0) { free(line); break; }
        if (strcmp(line, "quit") == 0) { free(line); break; }

        if (strcmp(line, "help") == 0) {
            printf("Commands work like a normal shell, but go through policy check.\n");
            printf("  exit, quit  - leave the shell\n");
            printf("  help        - show this message\n");
            free(line);
            continue;
        }

        cmdlock_run(line);
        free(line);
    }

    printf("\n");
    cmdlock_free();
    return 0;
}

static int cmd_policy(const char *repo) {
    /* Generate a default policy file */
    char path[512];
    snprintf(path, sizeof(path), "%s/.lockay", repo);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.lockay/policy", repo);

    printf("Writing default policy to %s\n", path);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "ERROR: cannot create %s\n", path);
        return 1;
    }

    fprintf(f, "# lockay command policy v1\n");
    fprintf(f, "# Format: <command_pattern>    <allow|ask|deny>\n");
    fprintf(f, "# Patterns use glob matching (* matches anything).\n");
    fprintf(f, "# First matching rule wins.\n\n");

    fprintf(f, "# --- L0: safe read-only ---\n");
    fprintf(f, "ls *              allow\n");
    fprintf(f, "cat *             allow\n");
    fprintf(f, "grep *            allow\n");
    fprintf(f, "echo *            allow\n");
    fprintf(f, "find *            allow\n");
    fprintf(f, "pytest *          allow\n");
    fprintf(f, "\n");
    fprintf(f, "# --- L1: local execution ---\n");
    fprintf(f, "python *          allow\n");
    fprintf(f, "python3 *         allow\n");
    fprintf(f, "make *            allow\n");
    fprintf(f, "gcc *             allow\n");
    fprintf(f, "\n");
    fprintf(f, "# --- L2: local write ---\n");
    fprintf(f, "mkdir *           allow\n");
    fprintf(f, "touch *           allow\n");
    fprintf(f, "cp *              ask\n");
    fprintf(f, "mv *              ask\n");
    fprintf(f, "git add *         ask\n");
    fprintf(f, "git commit *      ask\n");
    fprintf(f, "\n");
    fprintf(f, "# --- L3: destructive / network / dependency ---\n");
    fprintf(f, "rm *              ask\n");
    fprintf(f, "pip install *     ask\n");
    fprintf(f, "npm install *     ask\n");
    fprintf(f, "curl *            ask\n");
    fprintf(f, "wget *            ask\n");
    fprintf(f, "docker *          ask\n");
    fprintf(f, "\n");
    fprintf(f, "# --- L4: publish / system mutation ---\n");
    fprintf(f, "git push *        deny\n");
    fprintf(f, "ssh *             deny\n");
    fprintf(f, "sudo *            deny\n");
    fprintf(f, "chmod *           deny\n");
    fprintf(f, "\n");
    fprintf(f, "# --- Default ---\n");
    fprintf(f, "# *                 ask\n");

    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *prog = argv[0];

    if (argc < 2) {
        usage(prog);
        return 1;
    }

    const char *cmd   = argv[1];
    const char *repo  = get_repo_root();

    if (strcmp(cmd, "edit")   == 0) return cmd_edit(argc, argv, repo);
    if (strcmp(cmd, "show")   == 0) return cmd_show(argc, argv, repo);
    if (strcmp(cmd, "lock")   == 0) return cmd_lock(argc, argv, repo);
    if (strcmp(cmd, "unlock") == 0) return cmd_unlock(argc, argv, repo);
    if (strcmp(cmd, "status") == 0) return cmd_status(argc, argv, repo);
    if (strcmp(cmd, "apply")  == 0) return cmd_apply(argc, argv, repo);
    if (strcmp(cmd, "check")  == 0) return cmd_check(argc, argv, repo);
    if (strcmp(cmd, "set")    == 0) return cmd_set(argc, argv, repo);
    if (strcmp(cmd, "insert") == 0) return cmd_insert(argc, argv, repo);
    if (strcmp(cmd, "delete") == 0) return cmd_delete(argc, argv, repo);
    if (strcmp(cmd, "run")    == 0) return cmd_run(argc, argv, repo);
    if (strcmp(cmd, "shell")  == 0) return cmd_shell(repo);
    if (strcmp(cmd, "policy") == 0) return cmd_policy(repo);

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(prog);
    return 1;
}
