#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "optim.h"

// If the first argument is just "afl",
// then load the command line options from a newline-delimited file
// This makes it easy to fuzz with afl
static void handle_afl(int * argc, char *** argv) {
    if (*argc != 3) return;
    if (strcmp((*argv)[1], "afl") != 0) return;

    static char buffer[1 << 20];
    static char * new_argv[256];
    FILE * f = fopen((*argv)[2], "r");
    assert(f != NULL);
    size_t rc = fread(buffer, sizeof buffer, 1, f);
    (void) rc;
    fclose(f);
    buffer[sizeof(buffer) - 1] = '\0';
    char * sptr = NULL;
    new_argv[0] = (*argv)[0];
    new_argv[1] = strtok_r(buffer, "\n", &sptr);
    int new_argc = 2;
    for (size_t i = 2; i < sizeof new_argv; i++) {
        new_argv[i] = strtok_r(NULL, "\n", &sptr);
        if (new_argv[i] != NULL && new_argv[i][0] != '\0')
            new_argc++;
        else
            break;
    }

    *argc = new_argc;
    *argv = new_argv;
}

int main(int argc, char ** argv) {
    handle_afl(&argc, &argv);

    optim_t * o = optim_start(argc, argv, "[-a] [-b] <path>");
    assert(o != NULL);
    optim_error(o, "test\n");

    optim_usage(o, "My test optim program\n");
    optim_version(o, "optim_test Version 1.0\nAuthor: Zach Banks\n");

    optim_flag(o, 'v', "verbose", "Increase verbosity");
    //if (optim_get_count(o) > 0) optim_debug(o);

    optim_usage(o, "\nSection Two:\n");

    optim_arg(o, 'a', "alpha", NULL, "Alpha parameter. This usage has a lot to say, so the usage spans over multiple lines\nNewlines are also handled fine");
    while (optim_get_count(o) > 0)
        printf("Got alpha '%ld'\n", optim_get_long(o, -1));
    
    optim_flag(o, 'b', "beta", "Beta flag");

    optim_flag(o, 'c', NULL, "C flag without longform");

    optim_arg(o, 0, "delta", "diff", "Delta parameter without short form");
    optim_arg(o, 'e', NULL, "exarg", "Extra option with an arg but no longopt");

    optim_positionals(o);
    if (optim_get_count(o) < 1)
        optim_error(o, "expected at least one positional argument");

    while (optim_get_count(o) > 0)
        printf("Got positional '%s'\n", optim_get_string(o, "none"));

    int rc = optim_finish(&o);
    if (rc < 0) exit(EXIT_FAILURE);
}
