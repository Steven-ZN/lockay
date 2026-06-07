#ifndef CMDLOCK_H
#define CMDLOCK_H

#include <stdbool.h>

/* Risk levels for commands */
typedef enum {
    RISK_L0_SAFE      = 0,  /* read-only, no side effects */
    RISK_L1_EXEC      = 1,  /* runs local code */
    RISK_L2_WRITE     = 2,  /* modifies workspace files */
    RISK_L3_DESTRUCT  = 3,  /* destructive / network / dependency */
    RISK_L4_PUBLISH   = 4,  /* credential / publish / system mutation */
    RISK_UNKNOWN      = 99
} CmdRisk;

/* Policy decision */
typedef enum {
    DECISION_ALLOW = 0,
    DECISION_ASK   = 1,
    DECISION_DENY  = 2
} CmdDecision;

/* Parsed command information */
typedef struct {
    char *executable;     /* e.g., "rm" or "/usr/bin/rm" */
    char *raw_command;    /* full command string */
    int   argc;           /* argument count */
    char **argv;          /* argument vector (NULL-terminated) */
    bool  has_pipe;       /* contains | */
    bool  has_redirect;   /* contains > or >> */
    bool  has_append;     /* contains >> */
    char *redirect_path;  /* target path for > */
} CmdInfo;

/* Init / cleanup */
int  cmdlock_init(const char *repo_root);
void cmdlock_free(void);

/* Parse a command string into CmdInfo. Caller must free with cmd_info_free(). */
CmdInfo *cmd_parse(const char *command);
void     cmd_info_free(CmdInfo *ci);

/* Determine risk level for a command */
CmdRisk cmd_risk_level(const CmdInfo *ci);

/* Check command against policy. Returns DECISION_ALLOW/ASK/DENY. */
CmdDecision cmdlock_check(const CmdInfo *ci);

/* Execute a command after policy check. Returns exit code or -1. */
int cmdlock_run(const char *command);

/* Read a single command interactively (for shell mode) */
char *cmdlock_readline(void);

#endif
