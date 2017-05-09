// optim version 0.1
// Released under MIT License
// Copyright (c) 2017 Zach Banks

#include "optim.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define OPTIM_USAGE_WIDTH_ARGS 30
#define OPTIM_USAGE_WIDTH_HELP 50

#define OPTIM_INVALID (assert(0), fprintf(stderr, "Internal optim error: `%s` called with NULL `optim` parameter. Was `optim_finish` already called?\n", __func__), errno = EINVAL)
//#define OPTIM_INVALID assert(0);  // Alternatively, just crash

// The `assert` macro is used; but it is not required. It is OK to disable
// It is only used to validate internal state

struct optim {
    size_t argc;
    char ** argv;

    struct optim_arg * args;    // List of options
    struct optim_arg * invoc;   // Invocation

    bool started_options;
    bool asked_for_help;
    bool asked_for_version;
    bool takes_positionals;
    bool takes_unused;

    char cur_opt;
    const char * cur_longopt;
    int cur_count;
    struct optim_arg * cur_arg;

    char * error;               // First error message
    char * version;             // `--version` message
    FILE * usage;               // Usage/help message buffer
    char * usage_str;
    size_t usage_len;
    int usage_rc;               // Writes to `usage` are considered non-fatal
};

struct optim_arg {
    enum {
        TYPE_NONE,              // Empty
        TYPE_INVOC,             // Invocation; first argument
        TYPE_BARE,              // Does not start with "-" or is exactly "-"
        TYPE_FLAGS,             // Starts with just 1 '-'
        TYPE_LONG,              // Starts with "--" and does not have '='
        TYPE_LONG_ARG,          // Starts with "--" and has '='
        TYPE_SEP,               // Exactly "--"
    } type;
    char * arg;                 // Trimmed argument
    char * rhs;                 // Right hand side of arguments with '='; or basename for INVOC
    char last;                  // Last flag in a set of flags; or 0
    bool used;                  // Has this arg been consumed yet?
    struct optim_arg * next;    // Linked list of arguments for the same option
};

// Backup error string, if we fail to write an error string use this instead
static char * optim_bad_error_str = "Internal optim error: unable to write error string";

// Find the last occurance of `x` in string `str` of size `len`
static const char * strnrchr(const char * str, size_t len, char x) {
    assert(str != NULL && x != '\0');

    if (len == 0) return NULL;
    size_t i = len;
    while (i--) {
        if (str[i] == x) return &str[i]; 
    }
    return NULL;
}

optim_t * optim_start(int argc_, char ** argv, const char * example_usage) {
    if (argc_ < 0) return (errno = EINVAL, NULL);
    size_t argc = (size_t) argc_;

    struct optim * optim = calloc(1, sizeof *optim);
    if (optim == NULL) return NULL;

    // Leave an extra arg of TYPE_NONE at the end
    optim->args = calloc(argc + 1, sizeof *optim->args);
    if (optim->args == NULL) return (free(optim), NULL);

    optim->usage = open_memstream(&optim->usage_str, &optim->usage_len);
    if (optim->usage == NULL) return (free(optim->args), free(optim), NULL);

    // Done setting up: we can't fail now

    optim->argc = argc;
    optim->argv = argv;

    optim->cur_count = -1;

    // Parse the first arg (the invocation) specially
    optim->args[0].arg = argv[0];
    optim->args[0].type = TYPE_INVOC;
    char * basename = strrchr(argv[0], '/');
    if (basename == NULL)
        optim->args[0].rhs = argv[0];
    else
        optim->args[0].rhs = ++basename;
    optim->args[0].used = true;
    optim->invoc = &optim->args[0];

    // Parse remaining args
    bool found_sep = false;
    for (size_t i = 1; i < optim->argc; i++) {
        struct optim_arg * arg = &optim->args[i];
        arg->arg = argv[i];
        // It's OK if the caller deleted an argument by setting it to NULL
        if (arg->arg == NULL || arg->arg[0] == '\0') {
            arg->type = TYPE_NONE;
            continue;
        }
        if (found_sep || arg->arg[0] != '-' || arg->arg[1] == '\0') {
            arg->type = TYPE_BARE;
            continue;
        }
        arg->arg++;
        if (arg->arg[0] != '-') {
            arg->type = TYPE_FLAGS;
            arg->last = arg->arg[strlen(arg->arg)-1];
            continue;
        }
        arg->arg++;
        if (arg->arg[0] == '\0') {
            arg->type = TYPE_SEP;
            found_sep = true;
            continue;
        }

        arg->rhs = strchr(arg->arg, '=');
        if (arg->rhs == arg->arg) {
            // `argv[i]` started with "--=", treat it as a BARE
            arg->arg = argv[i];
            arg->type = TYPE_BARE;
        } else if (arg->rhs != NULL) {
            *arg->rhs++ = '\0';
            arg->type = TYPE_LONG_ARG;
        } else {
            arg->type = TYPE_LONG;
        }
    }

    // Start constructing usage message
    optim_usage(optim, "Usage: %s %s\n\n", optim->invoc->rhs, example_usage);

    return optim;
}

static void optim_check_unused(optim_t * optim) {
    assert(optim != NULL);
    for (size_t i = 0; i < optim->argc; i++) {
        struct optim_arg * arg = &optim->args[i];
        if (arg->used) continue;
        switch (arg->type) {
        case TYPE_NONE:
        case TYPE_INVOC:
        case TYPE_SEP:
            break;
        case TYPE_BARE:
            if (optim->takes_positionals)
                optim_error(optim, "Unused positional argument: '%s'", arg->arg);
            else
                optim_error(optim, "Unused floating argument: '%s'", arg->arg);
            break;
        case TYPE_FLAGS:
            assert(arg->arg != NULL && arg->arg[0] != '\0');
            optim_error(optim, "Unused flag: '-%c'", arg->arg[0]);
            break;
        case TYPE_LONG:
        case TYPE_LONG_ARG:
            assert(arg->arg != NULL && arg->arg[0] != '\0');
            optim_error(optim, "Unused argument: '--%s'", arg->arg);
            break;
        }
    }
}

int optim_finish(optim_t ** optim_p) {
    if (optim_p == NULL || *optim_p == NULL)
        return (OPTIM_INVALID, -1);

    optim_t * optim = *optim_p;
    optim_check_unused(optim);

    fflush(optim->usage);

    int rc = optim->error == NULL ? 0 : -1;
    if (optim->asked_for_help) {
        fprintf(stdout, "%s", optim->usage_str);
        rc = 1;
    } else if (optim->asked_for_version) {
        fprintf(stdout, "%s", optim->version);
        rc = 1;
    } else if (rc != 0) {
        fprintf(stderr, "Error: %s\n%s", optim->error, optim->usage_str);
    }

    // Cleanup!
    if (optim->error != optim_bad_error_str)
        free(optim->error);
    free(optim->version);
    fclose(optim->usage);
    free(optim->usage_str);
    free(optim->args);
    free(optim);
    // NULL-out optim to prevent calls to other methods
    *optim_p = NULL;

    return rc;
}

// -- Declaring Options --

// Add usage message for the option
static void optim_option_usage(optim_t * optim, char opt, const char * longopt, const char * metavar, const char * help) {
    // Precondition validation
    assert(optim != NULL);
    assert(opt != '\0' || longopt != NULL);
    // `metavar` can be null if the option does not take an argument
    if (help == NULL)
        help = "";

    // Print section header if this is the first option
    if (!optim->started_options) {
        optim_usage(optim, "\nOptions:\n");
        optim->started_options = true;

        optim_flag(optim, 'h', "help", "Print this help message");
        if (optim_get_count(optim) > 0)
            optim->asked_for_help = true;
    }

    static char helpbuf[OPTIM_USAGE_WIDTH_HELP+1];
    static char padding[OPTIM_USAGE_WIDTH_ARGS+1];
    memset(padding, ' ', OPTIM_USAGE_WIDTH_ARGS);
    padding[OPTIM_USAGE_WIDTH_ARGS] = '\0';

    int col = 0; // This will be incorrect if optim_usage returns -1; but that's OK for now

    // Indent 2 spaces
    col += optim_usage(optim, "  ");

    // Print short options
    if (opt != '\0')
        col += optim_usage(optim, "-%c", opt);
    else
        col += optim_usage(optim, "  ");

    // Print comma if there is both a short & long option, otherwise metavar
    if (opt != '\0' && longopt != NULL)
        col += optim_usage(optim, ", ");
    else if (opt != '\0' && longopt == NULL && metavar != NULL)
        col += optim_usage(optim, " %s", metavar);
    else
        col += optim_usage(optim, "  ");

    // Print long option, possibly with =metavar
    if (longopt != NULL) {
        col += optim_usage(optim, "--%s", longopt);
        if (metavar != NULL)
            col += optim_usage(optim, "=%s", metavar);
    }

    // Space between option and description
    col += optim_usage(optim, "  ");

    // Add remaining padding
    if (col > 0 && col < OPTIM_USAGE_WIDTH_ARGS)
        col += optim_usage(optim, "%s", &padding[col]);

    bool first_line = true;
    ssize_t remaining_len = OPTIM_USAGE_WIDTH_HELP + OPTIM_USAGE_WIDTH_ARGS - col;
    if (remaining_len < 0 || remaining_len > OPTIM_USAGE_WIDTH_HELP) {
        optim_usage(optim, "\n");
        first_line = false;
    }

    const char * hptr = help;
    while (*hptr != '\0') {
        if (!first_line) {
            optim_usage(optim, "%s  ", padding);
            remaining_len = OPTIM_USAGE_WIDTH_HELP - 2;
        }
        assert(remaining_len <= OPTIM_USAGE_WIDTH_HELP);

        ssize_t nlen = -1;
        const char * newline = strchr(hptr, '\n');
        if (newline != NULL) {
            nlen = (newline - hptr);
            if (nlen > remaining_len)
                nlen = -1;
        }

        if (nlen < 0) {
            size_t hlen = strlen(hptr);
            if (remaining_len >= 0 && hlen > (size_t) remaining_len) {
                hlen = (size_t) remaining_len;
                const char * space = strnrchr(hptr, hlen, ' ');
                if (space != NULL)
                    nlen = (space - hptr);
            }
        }

        if (nlen < 0 || nlen > remaining_len) {
            // Give up trying to format
            optim_usage(optim, "%s\n", hptr);
            break;
        } else {
            assert(nlen <= remaining_len);
            memcpy(helpbuf, hptr, (size_t) nlen);
            helpbuf[nlen] = '\0';
            optim_usage(optim, "%s\n", helpbuf);
            hptr += nlen + 1;
        }

        first_line = false;
    }
}

// Treat `arg->arg` as a set of flags, and remove `x` if it exists
// Return `true` if `x` was in `str`
// Set `arg->used` if `arg->arg` is now empty
static bool arg_flagpop(struct optim_arg * arg, char x) {
    assert(arg != NULL);

    if (arg->used)
        return false;

    char * str = arg->arg;
    assert(str != NULL);
    assert(str[0] != '\0');

    char * last = str + strlen(str) - 1;
    assert(last != NULL);
    assert(last >= str);
    do {
        if (*str == x) {
            *str = *last;
            *last = '\0';
            *(last+1) = x;
            if (arg->arg[0] == '\0')
                arg->used = true;
            return true;
        }
    } while (*str++ != '\0');
    return false;
}

void optim_arg(optim_t * optim, char opt, const char * longopt, const char * metavar, const char * help) {
    if (optim == NULL) { OPTIM_INVALID; return; }

    if (opt == '\0' && longopt == NULL) {
        optim_error(optim, "Internal optim error: `%s` called without `opt` or `longopt`", __func__);
        return;
    }
    if (optim->takes_positionals) {
        optim_error(optim, "Internal optim error: `%s` called after `optim_positionals`", __func__);
        return;
    }
    if (optim->takes_unused) {
        optim_error(optim, "Internal optim error: `%s` called after `optim_unused`", __func__);
        return;
    }

    if (metavar == NULL)
        metavar = "ARG";

    optim_option_usage(optim, opt, longopt, metavar, help);
    optim->cur_opt = opt;
    optim->cur_longopt = longopt;
    optim->cur_count = 0;
    optim->cur_arg = NULL;

    // Need to preserve the order of the linked list
    struct optim_arg * last_arg = NULL;
    
    for (size_t i = 0; i < optim->argc; i++) {
        struct optim_arg * arg = &optim->args[i];
        struct optim_arg * next_arg = &optim->args[i+1];
        if (arg->used) continue;
        switch (arg->type) {
        case TYPE_NONE: 
        case TYPE_INVOC: 
        case TYPE_BARE: 
        case TYPE_SEP: 
            break;
        case TYPE_FLAGS:
            if (opt == '\0') break;
            if (opt != arg->last) break;
            if (!arg_flagpop(arg, opt)) {
                optim_error(optim, "Flag '-%c %s' already consumed", opt, metavar);
                break;
            }
            if (arg_flagpop(arg, opt)) {
                optim_error(optim, "Flag '-%c %s' specified multiple times in same argument", opt, metavar);
                break;
            }
            if (next_arg->used || next_arg->type != TYPE_BARE) {
                optim_error(optim, "Flag '-%c' is missing its argument", opt);
                break;
            }
            arg->used = true;
            next_arg->used = true;
            if (optim->cur_arg == NULL)
                optim->cur_arg = next_arg;
            if (last_arg != NULL)
                last_arg->next = next_arg;
            last_arg = next_arg;
            optim->cur_count++;
            break;
        case TYPE_LONG:
            if (longopt == NULL) break;
            if (strcmp(arg->arg, longopt) != 0) break;
            if (next_arg->used || next_arg->type != TYPE_BARE) {
                optim_error(optim, "Flag '--%s' is missing its argument", longopt);
                break;
            }
            arg->used = true;
            next_arg->used = true;
            if (optim->cur_arg == NULL)
                optim->cur_arg = next_arg;
            if (last_arg != NULL)
                last_arg->next = next_arg;
            last_arg = next_arg;
            optim->cur_count++;
            break;
        case TYPE_LONG_ARG:
            if (longopt == NULL) break;
            if (strcmp(arg->arg, longopt) != 0) break;
            arg->used = true;
            if (optim->cur_arg == NULL)
                optim->cur_arg = arg;
            if (last_arg != NULL)
                last_arg->next = arg;
            last_arg = arg;
            optim->cur_count++;
            break;
        }
    }
}

void optim_flag(optim_t * optim, char opt, const char * longopt, const char * help) {
    if (optim == NULL) { OPTIM_INVALID; return; }

    if (opt == '\0' && longopt == NULL) {
        optim_error(optim, "Internal optim error: `%s` called without `opt` or `longopt`", __func__);
        return;
    }
    if (optim->takes_positionals) {
        optim_error(optim, "Internal optim error: `%s` called after `optim_positionals`", __func__);
        return;
    }
    if (optim->takes_unused) {
        optim_error(optim, "Internal optim error: `%s` called after `optim_unused`", __func__);
        return;
    }

    optim_option_usage(optim, opt, longopt, NULL, help);
    optim->cur_opt = opt;
    optim->cur_longopt = longopt;
    optim->cur_count = 0;
    optim->cur_arg = NULL;

    for (size_t i = 0; i < optim->argc; i++) {
        struct optim_arg * arg = &optim->args[i];
        if (arg->used) continue;
        switch (arg->type) {
        case TYPE_NONE: 
        case TYPE_INVOC: 
        case TYPE_BARE: 
        case TYPE_SEP: 
            break;
        case TYPE_FLAGS:
            assert(arg->arg != NULL);
            if (opt == '\0') break;
            while (arg_flagpop(arg, opt))
                optim->cur_count++;
            break;
        case TYPE_LONG:
            assert(arg->arg != NULL);
            if (longopt == NULL) break;
            if (strcmp(arg->arg, longopt) != 0) break;
            optim->cur_count++;
            arg->used = true;
            break;
        case TYPE_LONG_ARG:
            assert(arg->arg != NULL);
            if (longopt == NULL) break;
            if (strcmp(arg->arg, longopt) != 0) break;
            optim_error(optim, "Flag '--%s' does not take an argument", arg->arg);
            break;
        }
    }
}

void optim_positionals(optim_t * optim) {
    if (optim == NULL) { OPTIM_INVALID; return; }

    if (optim->takes_unused) {
        optim_error(optim, "Internal optim error: `%s` called after `optim_unused`", __func__);
        return;
    }
    if (optim->takes_positionals)
        return;

    optim->takes_positionals = true;
    optim->cur_opt = '\0';
    optim->cur_longopt = NULL;
    optim->cur_count = 0;
    optim->cur_arg = NULL;

    struct optim_arg * last_arg = NULL;

    for (size_t i = 0; i < optim->argc; i++) {
        struct optim_arg * arg = &optim->args[i];
        if (arg->used) continue;
        if (arg->type != TYPE_BARE) continue;

        if (optim->cur_arg == NULL)
            optim->cur_arg = arg;
        if (last_arg != NULL)
            last_arg->next = arg;
        last_arg = arg;
        optim->cur_count++;
    }
}

void optim_unused(optim_t * optim) {
    if (optim == NULL) { OPTIM_INVALID; return; }

    if (optim->takes_unused)
        return;

    optim->takes_unused = true;
    optim->cur_opt = '\0';
    optim->cur_longopt = NULL;
    optim->cur_count = 0;
    optim->cur_arg = NULL;

    struct optim_arg * last_arg = NULL;

    for (size_t i = 0; i < optim->argc; i++) {
        struct optim_arg * arg = &optim->args[i];
        if (arg->used) continue;

        // Rehydrate stripped forms
        arg->arg = optim->argv[i];
        if (arg->rhs != NULL)
            arg->rhs[-1] = '=';

        if (optim->cur_arg == NULL)
            optim->cur_arg = arg;
        if (last_arg != NULL)
            last_arg->next = arg;
        last_arg = arg;
        optim->cur_count++;
    }
}

// -- Reading Options --

int optim_get_count(optim_t * optim) {
    if (optim == NULL)
        return (OPTIM_INVALID, -1);

    if (optim->cur_count < 0)
        optim_error(optim, "Internal optim error: `%s` called before `optim_arg`, `optim_flag`, `optim_positionals`, or `optim_unused`", __func__);

    return optim->cur_count;
}

const char * optim_get_string(optim_t * optim, const char * empty) {
    if (optim == NULL) { OPTIM_INVALID; return empty; }

    if (optim->cur_count < 0)  {
        optim_error(optim, "Internal optim error: `%s` called before `optim_arg`, `optim_flag`, `optim_positionals`, or `optim_unused`", __func__);
        return empty;
    }

    if (optim->cur_count == 0)
        return empty;

    optim->cur_count--;
    struct optim_arg * arg = optim->cur_arg;
    optim->cur_arg = arg->next;
    arg->used = true;
    
    if (optim->takes_unused)
        return arg->arg;

    switch (arg->type) {
    case TYPE_BARE:
        return arg->arg;
    case TYPE_LONG_ARG:
        return arg->rhs;
    case TYPE_NONE:
    case TYPE_INVOC:
    case TYPE_FLAGS:
    case TYPE_LONG:
    case TYPE_SEP:
        break;
    }

    // There was a logic error if we get here
    assert(0);
    optim_error(optim, "Internal optim error: `%s` unable to handle argument type '%d'", __func__, arg->type);
    return empty;
}

long optim_get_long(optim_t * optim, long empty) {
    if (optim == NULL)
        return (OPTIM_INVALID, empty);

    if (optim->cur_count < 0) {
        optim_error(optim, "Internal optim error: `%s` called before `optim_arg`, `optim_flag`, `optim_positionals`, or `optim_unused`", __func__);
        return empty;
    }

    const char * strarg = optim_get_string(optim, NULL);
    if (strarg == NULL) return empty;

    char * p = NULL;
    long rc = strtol(strarg, &p, 0);
    if (strarg[0] == '\0' || p == NULL || p[0] != '\0') {
        optim_error(optim, "Unable to parse number '%s'", strarg);
        return empty;
    }
    return rc;
}

// -- Error Handling & Usage --

int optim_usage(optim_t * optim, const char * fmt, ...) {
    if (optim == NULL)
        return (OPTIM_INVALID, -1);

    if (optim->usage_rc < 0)
        return optim->usage_rc;

    va_list args;
    va_start(args, fmt);
    int rc = vfprintf(optim->usage, fmt, args);
    va_end(args);

    if (rc < 0) optim->usage_rc = rc;

    return rc;
}

int optim_error(optim_t * optim, const char * fmt, ...) {
    if (optim == NULL)
        return (OPTIM_INVALID, -1);

    if (optim->error != NULL)
        return 0;

    va_list args;
    va_start(args, fmt);
    int rc = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (rc < 0)
        return (optim->error = optim_bad_error_str, -1);

    size_t size = ((size_t) rc) + 1;
    optim->error = calloc(1, size);
    if (optim->error == NULL)
        return (optim->error = optim_bad_error_str, -1);

    va_start(args, fmt);
    rc = vsnprintf(optim->error, size, fmt, args);
    va_end(args);
    if (rc < 0)
        return (free(optim->error), optim->error = optim_bad_error_str, -1);

    // Delete trailing newline
    if (rc > 0 && optim->error[rc-1] == '\n')
        optim->error[rc-1] = '\0';

    return rc;
}

int optim_version(optim_t * optim, const char * fmt, ...) {
    if (optim == NULL)
        return (OPTIM_INVALID, -1);

    if (optim->version != NULL)
        return -1;

    va_list args;
    va_start(args, fmt);
    int rc = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (rc < 0)
        return -1;

    size_t size = ((size_t) rc) + 1;
    optim->version = calloc(1, size);
    if (optim->version == NULL)
        return (optim->version = NULL, -1);

    va_start(args, fmt);
    rc = vsnprintf(optim->version, size, fmt, args);
    va_end(args);
    if (rc < 0)
        return (free(optim->version), optim->version= NULL, -1);

    optim_flag(optim, '\0', "version", "Print version information");
    if (optim_get_count(optim) > 0)
        optim->asked_for_version = true;

    return rc;
}
