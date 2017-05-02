[![Build Status](https://travis-ci.org/zbanks/optim.svg?branch=master)](https://travis-ci.org/zbanks/optim)

# `optim`
Immediate-mode command line option parsing for C.

## Features

- Immediate-mode: declare, read, and validate locally
- Auto-generated usage message (``--help``)
- Options parsing can be split over multiple functions
- Supports long and short options, with and without arguments
- Supports positional arguments and `--`
- Supports repeated arguments
- Plays nice with `help2man`
- Doesn't rely on macros or preprocessor trickery
- Opinionated only when it makes things simpler

### Example Generated Usage

See ``test/main.c``

```
Usage: optim_test [-a] [-b] <path>

My test optim program

Options:
  -h, --help                  Print this help message
  -v, --verbose               Increase verbosity

Section Two:
  -a, --alpha=ARG             Alpha parameter. This usage has a lot to say, so
                                the usage spans over multiple lines
                                Newlines are also handled fine
  -b, --beta                  Beta flag
  -c                          C flag without longform
      --delta=diff            Delta parameter without short form
  -e exarg                    Extra option with an arg but no longopt
```

### Supported Formats

Each line shows options that are parsed equivalently by `optim`.

- `-v`, `--verbose`
- `-vvv`, `-v -v -v`, `--verbose -vv`
- `-a ARG`, `--alpha ARG`, `--alpha=ARG`
- `-va ARG`, `-v -a ARG`, `--verbose --alpha ARG`

### Unsupported Formats

The following examples are *not* parsed correctly by `optim`.

- `-aARG`
- `-a=ARG`
- `-beta`
- `--alpha -ARG`, use `--alpha=-ARG` if your argument could begin with a `-`

Also, `optim` does not currently support:

- Optional arguments (`--alpha[=ARG]`)
- Suboptions (`-o,rw`) 
- Subcommands (`git show`)

## Example

```
int main(int argc, char ** argv) {
    optim_t * o = optim_start(argc, argv, "%s [-a] [-b|-c] <path>");
    if (o == NULL) exit(EXIT_FAILURE);

    optim_usage(o, "My test optim program\n");
    optim_version(o, "optim_test Version 1.0\nAuthor: Zach Banks\n");

    optim_flag(o, 'v', "verbose", "Increase verbosity");
    int verbosity = optim_get_count(o);

    optim_usage(o, "\nSection Two:\n");

    optim_arg(o, 'a', "alpha", NULL, "Alpha parameter");
    long alpha = optim_get_long(o, 0);
    
    optim_flag(o, 'b', "beta", "Beta flag");
    bool beta = optim_get_count(o) > 0;

    optim_flag(o, 'c', NULL, "C flag without longform");
    bool cflag = optim_get_count(o) > 0;

    if (cflag && beta)
        optim_error("cannot specify both -b and -c flags");

    optim_positionals(o);
    char * path = optim_get_string(o, NULL);
    if (path == NULL)
        optim_error(o, "must specify path");

    int rc = optim_finish(&o);
    if (rc < 0) exit(EXIT_FAILURE);

    // ...
}
```

## About

`optim` is licensed under the MIT license. Copyright (c) 2017 Zach Banks.
