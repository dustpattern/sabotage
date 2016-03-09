Sabotage your C code
====================

`sabotage` is a simple utility that injects failures into your C code so you
can observe how it "behaves" (read: crashes and burns miserably) under the most
unlikely circumstances. It is particularly useful for testing failures in
`malloc()`, `mmap()`, `read()`, and so on, but it works with anything that may
fail.

You can trigger failures anywhere in your code, or target specific files,
functions, and/or lines. You can make failures appear at random or
consistently. The possibilities are endless.

*Warning*: when coupled with tools like Valgrind, this utility _will_ find all
sorts of interesting bugs in your code.


What's the point?
-----------------

It's easy to code for the "expected". It's the "unexpected" that will hit you
hard and make you spend days debugging production issues. This utility will
make the unexpected happen so it can be dealt with before release. Use it well
and your code will be indestructible.

Of course, sometimes you just don't care. Maybe it's ok for your server to
crash and restart. In that case, may the force be with you.


How does it work?
-----------------

Feed C files to the `sabotage` preprocessor before compiling, like this:

    % sabotage main.c | cc -c -o main.o -x c -

Then link to the static library:

    % cc -o a.out main.o ... -lsabotage

Before execution, set the `SABOTAGE` environment variable to the targets you
want to sabotage. For instance:

    % export SABOTAGE="10% foo(); 100% main.c:230"
    % ./a.out

That means 10% failure rate in function `foo()` -- on average one in ten of all
applicable statements inside `foo()` will fail, at random -- and a 100% chance
of failure in `main.c` at line 230.

See `Makefile.sample` in this repository for some ideas on how to set up your
build.

The anatomy of a failure depends on the statement being sabotaged. If the
statement is

    p = malloc(size);

then a failure will cause `malloc()` to return `NULL` and set `errno` to
`ENOMEM`. Functions that return an error code directly will fail with `ENOMEM`.
System calls such as `open()` will fail with -1 and set `errno`to `ENOMEM`.


No really, how does it work?
----------------------------

The `sabotage` preprocessor is a fairly dumb Perl script full of regular
expressions. It looks for certain patterns in your code, such as calls to
`malloc()`, `open()`, etc, and injects some special sauce that will decide, at
runtime, whether a statement should be sabotaged or not. For instance, a
pattern such as

    err = foo();

will be replaced with

    err = SABOTAGE ? : foo();

where `SABOTAGE` is a macro defined as

    #define SABOTAGE __sabotage(__FILE__, __func__, __LINE__)

On failure, `__sabotage()` returns `ENOMEM`, otherwise zero.

Another pattern:

    p = malloc(size);

will be replaced with

    p = (errno = SABOTAGE) ? NULL : malloc(size);

The script makes no attempt to parse or understand your code. It is just
matching patterns. If `sabotage` does not recognize your coding style, it can
be easily hacked to include more patterns. Please share your improvements
afterwards!


How do I specify what needs to be sabotaged?
--------------------------------------------

You set the `SABOTAGE` environment variable before running your application.
The syntax is as follows:

    SABOTAGE="P% WHERE ; ..."

`P` is an integer between 0 and 100. It represents a failure rate, where 0%
means "never fails", and 100% means "always fails". Anything in between
represents a failure rate of `P` percent. If omitted, the failure rate is 100%.

`WHERE` specifies a file, function, line number(s), or a combination of the
above. It may also be left empty to symbolize the entire source code.

    WHERE := file.c
           | file.c: func()
           | file.c: line, line, ...
           | func()
           | (empty)

The default sabotage rate of any piece of code is 0% unless overridden by a
specific `P` value, `WHERE` clause, or both in the `SABOTAGE` environment
variable.

Multiple sabotage targets may be separated by semicolons.

Examples:

Make everything fail 1% of the time:

    SABOTAGE="1%"

Make specific lines in `main.c` fail at a 50% rate, and everything else at 1%:

    SABOTAGE="50% main.c:122,232; 1%"

Make everything fail at a 1% rate except for the `init()` function, which
should never fail:

    SABOTAGE="0% init(); 1%"

Order matters. Targets are evaluated from left to right, so the following
configurations are _not_ equivalent:

    # init() never fails, everything else fails at a 10% rate
    SABOTAGE="0% init(); 10%"

    # Everything fails at a 10% rate
    SABOTAGE="10%; 0% init()"

In general, you should go from the most specific target to the left, to the
most generic target to the right.


How do I change the error code returned during a failure?
---------------------------------------------------------

Set the `SABOTAGE_ERRNO` environment variable to the (numerical) error code of
your choice.

The default error code is `ENOMEM` (that is, the value of `ENOMEM` on your
system). This is a fairly generic error that can occur pretty much anywhere.


Known Issues
------------

- The `sabotage` preprocessor makes no attempt to understand your code at a
  level deeper than simple regular expressions. For this reason, `sabotage` may
  not be compatible (yet) with your coding style. The good news is that the
  script is easily hackable. New patterns can be added with little effort. As
  long as your code is consistent in style, it should be possible to use
  `sabotage`.

- Sabotaged statements lack "side effects". A side effect is, for instance:
  `foo(&x)` modifies `x` even in the event of an error. If that statement is
  sabotaged, `foo()` will simply not be executed.

- Probability values can only be integers. 1% is the lowest non-zero failure
  rate you can specify for a target. Sometimes this is too big, especially for
  targets that are executed in tight loops.


Licensing and Copying
---------------------

Everything is in the public domain. Please share any improvements you make.
Enjoy!
