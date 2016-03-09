#!/usr/bin/perl

#
# The "sabotage" C preprocessor.
#
# Use:
#    sabotage [--view] [--where] FILE
#
# Options:
#    --view   Pipe output to the `view' command.
#    --where  Only show which lines will be sabotaged.
#

#
# This work is in the Public Domain. Everyone is free to use, modify,
# republish, sell or give away this work without prior consent from anybody.
#
# This software is provided on an "AS IS" basis, without warranty of any kind.
# Use at your own risk! Under no circumstances shall the author(s) or
# contributor(s) be liable for damages resulting directly or indirectly from
# the use or non-use of this documentation.
#

use strict;

# input files
my @files = ( );

# redirect output to `view' command
my $view = 0;

# only show which lines are being sabotaged
my $where = 0;

#
# Sabotage file.
#
sub process
{
	my ($file)   = @_;
	my  $enabled = 1;
	my  $lineno  = 0;
	my  $fd;

	open ($fd, "$file")
		or die("cannot open $file");

	if (!$where) {
		printf("#include <sabotage.h>\n");
		printf("#line 1 \"%s\"\n", $file);
	}

L:	while (<$fd>)
	{
		my $line = $_;
		my $sbtline;
		$lineno++;

		# The @BeginNoSabotage annotation disables preprocessing
		if ($line =~ /\b@?BeginNoSabotage\b/) {
			$enabled = 0;
			goto SKIP;
		}
		# The annotation @EndNoSabotage reenables preprocessing
		elsif ($line =~ /\b@?EndNoSabotage\b/) {
			$enabled = 1;
		}
		elsif ($enabled eq 0) {
			goto SKIP;
		}
		#
		# The @NoSabotage annotation prevents the current line from being
		# sabotaged -- but a better way is to use the SABOTAGE environment
		# variable. We also skip lines that contain the string "SABOTAGE" in
		# case the source makes explicit use of the "SABOTAGE" macro, for
		# instance:
		#
		#     #ifndef SABOTAGE
		#     #define SABOTAGE 0
		#     #endif
		#
		#     err = SABOTAGE ? EIO : foo();
		#
		elsif ($line =~ /\b@?(NoSabotage|SABOTAGE)\b/) {
			goto SKIP;
		}

		#
		# Functions that return a non-zero system error code on error,
		# and zero on success.
		#
		# Examples:
		#
		#    err = foo();
		#
		#    if ((err = foo())) ...
		#
		# We look specifically for a variable named `err'. This is purely
		# arbitrary. The assumption is that `err' is limited in value to system
		# error codes (ENOMEM, EPERM, EIO, etc) rather than custom error codes.
		# Of course this is a big assumption. Feel free to rename `err' to
		# something else, or introduce more logic here.
		#
		if ($line =~ /^(.*)\berr\s*=\s*([[:alpha:]_][[:alnum:]_]*\(.*)$/) {
			my ($prefix, $suffix) = ($1, $2);
			$sbtline = sprintf(
				"%serr = SABOTAGE ? : %s",
				$prefix, $suffix);
		}
		elsif ($line =~ /^(\s*)if\s*\(\s*\(\s*err\s*=\s*(.*)/) {
			my ($prefix, $suffix) = ($1, $2);
			$sbtline = sprintf(
				"%sif ((err = SABOTAGE ? : %s",
				$prefix, $suffix);
		}

		#
		# Functions that return NULL on failure.
		#
		# Examples:
		#
		#    if ((p = malloc(...)) == NULL) ...
		#
		#    if ((f = fopen(...)) == NULL) ...
		#
		#    if (mkdtemp(...) == NULL) ...
		#
		elsif ($line =~ /^(.*)=\s*((?:fopen|freopen|malloc|mkdtemp|realloc|strdup|valloc|opendir)\b.*)$/) {
			my ($prefix, $suffix) = ($1, $2);
			$sbtline = sprintf(
				"%s= (errno = SABOTAGE) ? NULL : %s",
				$prefix, $suffix);
		}
		#
		# A special case for mmap(), which returns MAP_FAILED in case of an error.
		#
		elsif ($line =~ /^(.*)=\s*(mmap\(.*)$/) {
			my ($prefix, $suffix) = ($1, $2);
			$sbtline = sprintf(
				"%s= (errno = SABOTAGE) ? MAP_FAILED : %s",
				$prefix, $suffix);
		}

		#
		# Functions that return -1 on error, and 0 on success.
		#
		# For instance:
		#
		#    if (stat("foo", &stbuf)) ...
		#
		#    if (mkdir("hello", S_IRWXU)) ...
		#
		elsif ($line =~ /^(.*)\bif\s*\(\s*(access|chdir|fstat|ftruncate|gethostname|mkdir|pipe|pipe2|rename|stat)\b(.*)$/) {
			my ($prefix, $syscall, $suffix) = ($1, $2, $3);
			$sbtline = sprintf(
				"%sif ((errno = SABOTAGE) ? -1 : %s%s",
				$prefix, $syscall, $suffix);
		}

		#
		# Functions that return -1 on error, and >= 0 on success.
		#
		# Examples:
		#
		#    if ((fd = open(...))) ...
		#
		#    pid = fork();
		#
		elsif ($line =~ /^(.*)\bif\s*\(\s*\(\s*(\S+)\s*=\s*(fork|mkstemp|open|read|write)\((.*)$/) {
			my ($prefix, $var, $func, $suffix) = ($1, $2, $3, $4);
			$sbtline = sprintf(
				"%sif ((%s = (errno = SABOTAGE) ? -1 : %s(%s",
				$prefix, $var, $func, $suffix);
		}
		elsif ($line =~ /^(.*)\b(\w+)\s*=\s*(read|write|fork)\((.*)$/) {
			my ($prefix, $var, $func, $suffix) = ($1, $2, $3, $4);
			$sbtline = sprintf(
				"%s%s = (errno = SABOTAGE) ? -1 : %s(%s",
				$prefix, $var, $func, $suffix);
		}

		#
		# When we see an `if' statement that we don't recognize, allow the user
		# to specify how to sabotage it using the "@Sabotage EXPR" annotation.
		# For instance;
		#
		#    if (memcmp(a, b, sizeof(a)))   // Sabotage 1
		#    if (unknown_syscall() < 0)     // Sabotage errno=EIO
		#
		# We can also sabotage simple assignments such as:
		#
		#    ret = foo();  // Sabotage -1
		#
		elsif ( $line =~ /\/\/\s*@?Sabotage\s*(\S.*)\s*$/ ) {
			my ($expr) = ($1);
			if ( $line =~ /^(\s+(?:else\s+)?)if\s*\((.*)\/\// ) {
				my ($prefix, $suffix) = ($1, $2);
				$sbtline = sprintf(
					"%sif (SABOTAGE ? (%s) : %s",
					$prefix, $expr, $suffix);
			}
			elsif ( $line =~ /^(.*)=(.*)$/ ) {
				my ($prefix, $suffix) = ($1, $2);
				$sbtline = sprintf(
					"%s= SABOTAGE ? (%s) :%s",
					$prefix, $expr, $suffix);
			}
		}

	SKIP:
		if ($sbtline)
		{
			if ($where)
			{
				my $trim = $line;
				$trim =~ s/^\s+//;
				$trim =~ s/\s+$//;
				printf(STDERR "%5u: %s\n", $lineno, $trim);
			}
			else
			{
				printf("%s /* Line %u */\n", $sbtline, $lineno);
			}
		}
		elsif (!$where)
		{
			print($line);
		}
	}

	close($fd);
}

#
# Parse command-line arguments.
#
sub init
{
	my $i;
	my $argc = $#ARGV + 1;

	for ( $i = 0 ; $i < $argc; $i++ ) {
		my $arg = $ARGV[$i];
		$arg =~ s/^\s+//;
		$arg =~ s/\s+$//;

		# input file
		if (-f $arg) {
			@files = ( @files, $arg );
		}
		# view
		elsif ($arg =~ /^--?view$/) {
			$view = 1;
		}
		# where
		elsif ($arg =~ /^--?w(here)?$/) {
			$where = 1;
		}
		# help
		elsif ($arg =~ /^--?h(elp)?$/) {
			printf (STDERR "sabotage [--view] [--where] file1 [file2 ...]\n");
			exit 0;
		}
		# invalid argument
		elsif ($arg =~ /^-/) {
			printf (STDERR "Invalid option `%s'\n", $arg);
			printf (STDERR "See -help for more information.\n");
			exit 1;
		}
	}

	# check for input files
	my $nfiles = @files;
	if ($nfiles eq 0) {
		printf (STDERR "No input files.\n");
		printf (STDERR "See -help\n");
		exit 1;
	}
}

#
# Main function.
#
sub main
{
	init();

	# redirect output to `view' command if desired
	if ($view eq 1) {
		open (STDOUT, "| view -c 'set syntax=c' -") ||
			die ("cannot run 'view' command");
	}

	# process files
	foreach my $file (@files) {
		if ($file =~ /\.c$/) {
			process($file);
		}
	}

	close (STDOUT);
}

main();
