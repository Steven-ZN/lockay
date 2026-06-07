#include "cmdlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

/* ========== Policy engine ========== */

#define POLICY_MAX_RULES 256
#define POLICY_PATH_MAX  512
#define RULE_PATTERN_MAX 256

typedef struct {
    char   pattern[RULE_PATTERN_MAX];
    CmdDecision decision;
} PolicyRule;

static PolicyRule policy_rules[POLICY_MAX_RULES];
static int policy_count = 0;
static char policy_repo_root[POLICY_PATH_MAX];
static bool policy_loaded = false;

/* Default built-in policy (used if no policy file exists) */
static const char *default_policy[] = {
    "ls *            allow",
    "cat *           allow",
    "grep *          allow",
    "echo *          allow",
    "find *          allow",
    "file *          allow",
    "head *          allow",
    "tail *          allow",
    "wc *            allow",
    "sort *          allow",
    "uniq *          allow",
    "diff *          allow",
    "man *           allow",
    "which *         allow",
    "pwd *           allow",
    "whoami *        allow",
    "uname *         allow",
    "env *           allow",
    "pytest *        allow",
    "python *        allow",
    "python3 *       allow",
    "node *          allow",
    "make *          allow",
    "gcc *           allow",
    "g++ *           allow",
    "clang *         allow",
    "mkdir *         allow",
    "touch *         allow",
    "mkdir *         allow",
    "cp *            ask",
    "mv *            ask",
    "git status *    allow",
    "git diff *      allow",
    "git log *       allow",
    "git branch *    allow",
    "git add *       ask",
    "git commit *    ask",
    "git checkout *  ask",
    "git merge *     ask",
    "git rebase *    ask",
    "git push *      deny",
    "git push --force * deny",
    "rm *            ask",
    "rm -rf *        ask",
    "pip install *   ask",
    "pip3 install *  ask",
    "npm install *   ask",
    "curl *          ask",
    "wget *          ask",
    "docker *        ask",
    "sudo *          deny",
    "ssh *           deny",
    "scp *           deny",
    "chmod *         deny",
    "chown *         deny",
    "kill *          deny",
    "systemctl *     deny",
    "mount *         deny",
};
#define DEFAULT_POLICY_COUNT \
    (sizeof(default_policy) / sizeof(default_policy[0]))

static void load_default_policy(void) {
    policy_count = 0;
    for (size_t i = 0; i < DEFAULT_POLICY_COUNT && policy_count < POLICY_MAX_RULES; i++) {
        const char *line = default_policy[i];
        /* Parse: pattern  decision */
        const char *last_space = strrchr(line, ' ');
        if (!last_space) continue;

        /* Last word is the decision */
        const char *decision_str = last_space + 1;
        /* Pattern is everything before */
        size_t patlen = (size_t)(last_space - line);

        if (patlen >= RULE_PATTERN_MAX) patlen = RULE_PATTERN_MAX - 1;
        memcpy(policy_rules[policy_count].pattern, line, patlen);
        policy_rules[policy_count].pattern[patlen] = '\0';

        /* Trim trailing spaces from pattern */
        while (patlen > 0 && policy_rules[policy_count].pattern[patlen - 1] == ' ') {
            policy_rules[policy_count].pattern[--patlen] = '\0';
        }

        if (strcmp(decision_str, "allow") == 0)
            policy_rules[policy_count].decision = DECISION_ALLOW;
        else if (strcmp(decision_str, "ask") == 0)
            policy_rules[policy_count].decision = DECISION_ASK;
        else
            policy_rules[policy_count].decision = DECISION_DENY;

        policy_count++;
    }
}

static int load_policy_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    policy_count = 0;
    char line[512];
    while (fgets(line, sizeof(line), f) && policy_count < POLICY_MAX_RULES) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip comments and empty lines */
        if (len == 0 || line[0] == '#')
            continue;

        /* Parse: pattern  decision */
        const char *last_space = strrchr(line, ' ');
        if (!last_space) continue;

        const char *decision_str = last_space + 1;
        size_t patlen = (size_t)(last_space - line);
        if (patlen >= RULE_PATTERN_MAX) patlen = RULE_PATTERN_MAX - 1;

        memcpy(policy_rules[policy_count].pattern, line, patlen);
        policy_rules[policy_count].pattern[patlen] = '\0';

        while (patlen > 0 && policy_rules[policy_count].pattern[patlen - 1] == ' ') {
            policy_rules[policy_count].pattern[--patlen] = '\0';
        }

        if (strcmp(decision_str, "allow") == 0)
            policy_rules[policy_count].decision = DECISION_ALLOW;
        else if (strcmp(decision_str, "ask") == 0)
            policy_rules[policy_count].decision = DECISION_ASK;
        else
            policy_rules[policy_count].decision = DECISION_DENY;

        policy_count++;
    }

    fclose(f);
    return 0;
}

int cmdlock_init(const char *repo_root) {
    strncpy(policy_repo_root, repo_root, sizeof(policy_repo_root) - 1);
    policy_repo_root[sizeof(policy_repo_root) - 1] = '\0';

    /* Try to load custom policy */
    char policy_path[POLICY_PATH_MAX];
    snprintf(policy_path, sizeof(policy_path), "%s/.lockay/policy", repo_root);

    if (load_policy_file(policy_path) == 0) {
        policy_loaded = true;
        return 0;
    }

    /* Fall back to default policy */
    load_default_policy();
    policy_loaded = true;
    return 0;
}

void cmdlock_free(void) {
    policy_count = 0;
    policy_loaded = false;
}

/* ========== Command parsing ========== */

CmdInfo *cmd_parse(const char *command) {
    CmdInfo *ci = calloc(1, sizeof(*ci));
    if (!ci) return NULL;

    ci->raw_command = strdup(command);

    /* Allocate argv */
    int argv_cap = 64;
    ci->argv = calloc((size_t)argv_cap + 1, sizeof(char *));

    const char *p = command;
    while (*p) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Check for pipe and redirect BEFORE processing the arg */
        if (*p == '|') {
            ci->has_pipe = true;
            p++;
            continue;
        }
        if (*p == '>') {
            ci->has_redirect = true;
            p++;
            if (*p == '>') { ci->has_append = true; p++; }
            /* Read redirect target */
            while (*p && isspace((unsigned char)*p)) p++;
            const char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '|' && *p != '>') p++;
            size_t rlen = (size_t)(p - start);
            ci->redirect_path = malloc(rlen + 1);
            memcpy(ci->redirect_path, start, rlen);
            ci->redirect_path[rlen] = '\0';
            continue;
        }

        /* Parse argument (handle quotes) */
        char arg[1024];
        int alen = 0;
        char quote = 0;

        while (*p && alen < (int)sizeof(arg) - 1) {
            if (quote) {
                if (*p == quote) { quote = 0; p++; continue; }
                if (*p == '\\' && *(p+1)) { arg[alen++] = *(p+1); p += 2; continue; }
                arg[alen++] = *p++;
            } else {
                if (*p == '"' || *p == '\'') { quote = *p; p++; continue; }
                if (*p == '\\' && *(p+1)) { arg[alen++] = *(p+1); p += 2; continue; }
                if (isspace((unsigned char)*p) || *p == '|' || *p == '>') break;
                arg[alen++] = *p++;
            }
        }
        arg[alen] = '\0';

        if (alen > 0) {
            if (ci->argc >= argv_cap) {
                argv_cap *= 2;
                ci->argv = realloc(ci->argv, (size_t)(argv_cap + 1) * sizeof(char *));
            }
            ci->argv[ci->argc] = strdup(arg);
            ci->argc++;
        }
    }
    ci->argv[ci->argc] = NULL;

    if (ci->argc > 0)
        ci->executable = strdup(ci->argv[0]);

    return ci;
}

void cmd_info_free(CmdInfo *ci) {
    if (!ci) return;
    free(ci->raw_command);
    free(ci->executable);
    for (int i = 0; i < ci->argc; i++)
        free(ci->argv[i]);
    free(ci->argv);
    free(ci->redirect_path);
    free(ci);
}

/* ========== Risk level ========== */

/* Check if executable matches any of the given names */
static bool exe_is(const CmdInfo *ci, const char *name) {
    if (!ci->executable) return false;

    /* Check full path: /usr/bin/rm -> rm */
    const char *base = strrchr(ci->executable, '/');
    const char *exe = base ? base + 1 : ci->executable;

    return strcmp(exe, name) == 0;
}

CmdRisk cmd_risk_level(const CmdInfo *ci) {
    if (!ci->executable) return RISK_UNKNOWN;

    /* L4: publish / system mutation */
    if (exe_is(ci, "sudo") || exe_is(ci, "su") ||
        exe_is(ci, "ssh") || exe_is(ci, "scp") ||
        exe_is(ci, "chmod") || exe_is(ci, "chown") ||
        exe_is(ci, "kill") || exe_is(ci, "killall") ||
        exe_is(ci, "systemctl") || exe_is(ci, "service") ||
        exe_is(ci, "mount") || exe_is(ci, "umount"))
        return RISK_L4_PUBLISH;

    if (exe_is(ci, "git")) {
        /* git push → L4, git commit → L3, others → L2 */
        for (int i = 1; i < ci->argc; i++) {
            if (strcmp(ci->argv[i], "push") == 0)
                return RISK_L4_PUBLISH;
            if (strcmp(ci->argv[i], "commit") == 0)
                return RISK_L3_DESTRUCT;
            if (strcmp(ci->argv[i], "rebase") == 0)
                return RISK_L3_DESTRUCT;
            if (strcmp(ci->argv[i], "reset") == 0)
                return RISK_L3_DESTRUCT;
        }
        return RISK_L2_WRITE;
    }

    /* L3: destructive / network / dependency */
    if (exe_is(ci, "rm") || exe_is(ci, "rmdir") ||
        exe_is(ci, "pip") || exe_is(ci, "pip3") ||
        exe_is(ci, "npm") || exe_is(ci, "yarn") ||
        exe_is(ci, "curl") || exe_is(ci, "wget") ||
        exe_is(ci, "docker") || exe_is(ci, "podman") ||
        exe_is(ci, "shred"))
        return RISK_L3_DESTRUCT;

    /* L2: local write */
    if (exe_is(ci, "touch") || exe_is(ci, "mkdir") ||
        exe_is(ci, "cp") || exe_is(ci, "mv") ||
        exe_is(ci, "dd") || exe_is(ci, "tee"))
        return RISK_L2_WRITE;

    /* L1: execution */
    if (exe_is(ci, "python") || exe_is(ci, "python3") ||
        exe_is(ci, "node") || exe_is(ci, "ruby") ||
        exe_is(ci, "perl") || exe_is(ci, "bash") || exe_is(ci, "sh") ||
        exe_is(ci, "make") || exe_is(ci, "gcc") || exe_is(ci, "g++") ||
        exe_is(ci, "clang") || exe_is(ci, "go") || exe_is(ci, "rustc") ||
        exe_is(ci, "javac") || exe_is(ci, "pytest") ||
        exe_is(ci, "cargo") || exe_is(ci, "cmake") || exe_is(ci, "ninja"))
        return RISK_L1_EXEC;

    /* L0: safe */
    return RISK_L0_SAFE;
}

/* ========== Policy matching ========== */

static bool match_pattern(const char *pattern, const char *command) {
    /* Simple glob matching: * matches anything */
    if (strcmp(pattern, "*") == 0) return true;

    const char *cp = command;
    const char *pp = pattern;

    while (*pp && *cp) {
        if (*pp == '*') {
            pp++;
            if (!*pp) return true;  /* trailing * matches everything */

            /* Find the next part after * */
            while (*cp) {
                if (match_pattern(pp, cp)) return true;
                cp++;
            }
            return false;
        }
        if (*pp == *cp) {
            pp++; cp++;
        } else {
            return false;
        }
    }

    /* Handle trailing * */
    if (*pp == '*') pp++;

    return *pp == '\0' && *cp == '\0';
}

CmdDecision cmdlock_check(const CmdInfo *ci) {
    if (!ci->raw_command) return DECISION_ASK;

    /* Walk policy rules in order (first match wins) */
    for (int i = 0; i < policy_count; i++) {
        if (match_pattern(policy_rules[i].pattern, ci->raw_command))
            return policy_rules[i].decision;
    }

    /* Default: ask */
    return DECISION_ASK;
}

/* ========== Audit logging ========== */

static void audit_log(const CmdInfo *ci, CmdDecision decision,
                      const char *user_action, int exit_code) {
    char dotdir[POLICY_PATH_MAX + 16];
    snprintf(dotdir, sizeof(dotdir), "%s/.lockay", policy_repo_root);
    mkdir(dotdir, 0755);

    char logpath[POLICY_PATH_MAX + 48];
    snprintf(logpath, sizeof(logpath), "%s/audit.log", dotdir);

    FILE *f = fopen(logpath, "a");
    if (!f) return;

    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));

    const char *decision_str =
        decision == DECISION_ALLOW ? "ALLOW" :
        decision == DECISION_ASK   ? "ASK"   : "DENY";

    const char *risk_str[] = {"L0","L1","L2","L3","L4","??"};
    CmdRisk risk = cmd_risk_level(ci);

    fprintf(f, "%s | %s | risk=%s | action=%s | exit=%d | %s\n",
            ts, decision_str,
            risk_str[risk <= RISK_L4_PUBLISH ? risk : 5],
            user_action ? user_action : "-",
            exit_code,
            ci->raw_command);

    fclose(f);
}

/* ========== Command execution ========== */

static void print_audit_prompt(const CmdInfo *ci, CmdDecision decision) {
    CmdRisk risk = cmd_risk_level(ci);
    const char *risk_labels[] = {
        "SAFE", "EXEC", "WRITE", "DESTRUCT", "PUBLISH", "UNKNOWN"
    };
    const char *risk_label = risk_labels[risk <= RISK_L4_PUBLISH ? risk : 5];

    fprintf(stderr, "\n--- lockay: command gate ---\n");
    fprintf(stderr, "Command : %s\n", ci->raw_command);
    fprintf(stderr, "Risk    : L%d (%s)\n", risk, risk_label);
    if (ci->has_pipe)    fprintf(stderr, "Note    : contains pipe\n");
    if (ci->has_redirect) fprintf(stderr, "Note    : contains redirect\n");
    fprintf(stderr, "Policy  : %s\n",
            decision == DECISION_ALLOW ? "ALLOW" :
            decision == DECISION_DENY  ? "DENY"  : "ASK");
    fprintf(stderr, "-----------------------------\n");
}

int cmdlock_run(const char *command) {
    if (!policy_loaded) {
        fprintf(stderr, "ERROR: cmdlock not initialized\n");
        return -1;
    }

    CmdInfo *ci = cmd_parse(command);
    if (!ci || !ci->executable) {
        fprintf(stderr, "ERROR: failed to parse command\n");
        cmd_info_free(ci);
        return -1;
    }

    CmdDecision decision = cmdlock_check(ci);
    print_audit_prompt(ci, decision);

    if (decision == DECISION_DENY) {
        fprintf(stderr, "DENIED: command blocked by policy\n");
        audit_log(ci, decision, "denied", -1);
        cmd_info_free(ci);
        return -1;
    }

    if (decision == DECISION_ASK) {
        fprintf(stderr, "This command requires approval.\n");
        fprintf(stderr, "Options: [A]llow once  [D]eny  [V]iew risk  ");
        fflush(stderr);

        char response[16];
        if (!fgets(response, sizeof(response), stdin)) {
            audit_log(ci, decision, "denied (EOF)", -1);
            cmd_info_free(ci);
            return -1;
        }

        /* Strip newline */
        size_t rlen = strlen(response);
        if (rlen > 0 && response[rlen-1] == '\n') response[--rlen] = '\0';

        if (response[0] == 'A' || response[0] == 'a' || response[0] == 'y' || response[0] == 'Y') {
            /* Allow */
        } else if (response[0] == 'D' || response[0] == 'd' || response[0] == 'n' || response[0] == 'N') {
            fprintf(stderr, "DENIED by user\n");
            audit_log(ci, decision, "denied by user", -1);
            cmd_info_free(ci);
            return -1;
        } else {
            fprintf(stderr, "DENIED (unrecognized response)\n");
            audit_log(ci, decision, "denied (invalid)", -1);
            cmd_info_free(ci);
            return -1;
        }
    }

    /* Execute the command via /bin/sh */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child */
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    } else if (pid < 0) {
        fprintf(stderr, "ERROR: fork failed: %s\n", strerror(errno));
        audit_log(ci, decision, "fork failed", -1);
        cmd_info_free(ci);
        return -1;
    }

    int status;
    waitpid(pid, &status, 0);

    int exit_code = -1;
    if (WIFEXITED(status))
        exit_code = WEXITSTATUS(status);

    audit_log(ci, decision, "executed", exit_code);
    cmd_info_free(ci);
    return exit_code;
}

char *cmdlock_readline(void) {
    printf("lockay> ");
    fflush(stdout);

    char *line = NULL;
    size_t len = 0;
    if (getline(&line, &len, stdin) == -1) {
        free(line);
        return NULL;
    }

    /* Strip newline */
    size_t l = strlen(line);
    if (l > 0 && line[l-1] == '\n') line[--l] = '\0';
    if (l == 0) { free(line); return strdup(""); }

    return line;
}
