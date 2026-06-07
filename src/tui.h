#ifndef TUI_H
#define TUI_H

/* Launch the nano-style TUI editor for a file.
 * repo_root: path to the repository root (for lockdb)
 * file: relative file path within the repo
 * Returns 0 on success, non-zero on error. */
int tui_edit(const char *repo_root, const char *file);

#endif
