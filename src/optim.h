#ifndef __OPTIM_H__
#define __OPTIM_H__

// optim version 0.1
// Released under MIT License
// Copyright (c) 2017 Zach Banks

typedef struct optim optim_t;

// Create an optim instance from `argc` and `argv`
// `usage` is a one-line description how to invoke the program
// and will be prefixed with the basename. Do not end in '\n'
// optim will take ownership of `argv`, and may modify its contents
optim_t * optim_start(int argc, char ** argv, const char * usage); 

// Finish parsing the options & destroy `*optim_p`, setting it to NULL
// Returns `0` on success, `-1` on error, and `1` if usage was printed.
// Your program should exit(EXIT_FAILURE) if the return is non-zero.
int optim_finish(optim_t ** optim_p);

// -- Declaring Options --

// Delcare an option that takes a required argument
// `opt`        - 1-letter short option (-l), or `\0` for long-only
// `longopt`    - long option (--long), or `NULL` for short-only
// `metavar`    - name of argument in usage (--long=metavar)
// `help`       - usage message, can contain newlines
void optim_arg(optim_t * optim, char opt, const char * longopt, const char * metavar, const char * help);

// Delcare an option that does not take an argument
// `opt`        - 1-letter short option (-l), or `\0` for long-only
// `longopt`    - long option (--long), or `NULL` for short-only
// `help`       - usage message, can contain newlines
void optim_flag(optim_t * optim, char opt, const char * longopt, const char * help);

// Take positional arguments
// This function should only be called after all other `optim_arg` and `optim_flag`s
void optim_positionals(optim_t * optim);

// Take unused/invalid arguments
// This function should only be called after you've used all other arguments
// If you consume an argument with `optim_get_string`, optim will consider it used
void optim_unused(optim_t * optim);

// -- Reading Options --

// Get the number of instances remaining of the current option
// Returns `-1` if optim is in an invalid state
// Each call to `optim_get_string` or `optim_get_long` decrements this by 1
int optim_get_count(optim_t * optim);

// Get the argument to the current option as a string
// Returns `empty` if it is not available
const char * optim_get_string(optim_t * optim, const char * empty);

// Get the argument to the current option as a long
// Returns `empty` if it is not available (or there is a parse error)
long optim_get_long(optim_t * optim, long empty);

// -- Error Handling & Usage --

// Declare an error
// If there are any errors, the first error message is printed, followed by the usage
// and `optim_finish` will return `-1` when called.
// The error message supports printf-style string interpolation.
__attribute__ ((format (printf, 2, 3)))
int optim_error(optim_t * optim, const char * format, ...);

// Add text to the usage message
// The message supports printf-style string interpolation.
__attribute__ ((format (printf, 2, 3)))
int optim_usage(optim_t * optim, const char * format, ...);

// Set the `--version` text, useful for using `help2man`
// The message supports printf-style string interpolation.
__attribute__ ((format (printf, 2, 3)))
int optim_version(optim_t * optim, const char * format, ...);

#endif
