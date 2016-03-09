#!/usr/bin/env bash
#
# See `sabotage-conf --help' for usage information.
#
# This work is in the Public Domain. Everyone is free to use, modify,
# republish, sell or give away this work without prior consent from anybody.
#
# This software is provided on an "AS IS" basis, without warranty of any kind.
# Use at your own risk! Under no circumstances shall the author(s) or
# contributor(s) be liable for damages resulting directly or indirectly from
# the use or non-use of this documentation.
#

declare -r PROC=$(basename "$0")
declare    INCDIR
declare    LIBDIR

# Print usage information
function help
{
	{
	echo "Usage:"
	echo "   sabotage-conf [OPTION]"
	echo
	echo "Wheere OPTION is:"
	echo "   --libs      print library linking information"
	echo "   --cflags    print preprocessor and compiler flags"
	echo "   --help      print this help"
	} >&2
	exit 64
}

# Determine installation directory
function setup
{
	cd $(dirname "$0") || exit $?
	if [ -f libsabotage.a ]
	then
		INCDIR="$PWD"
		LIBDIR="$PWD"
	elif [ -f ../lib/libsabotage.a ]
	then
		cd .. || exit $?
		INCDIR="$PWD/include"
		LIBDIR="$PWD/lib"
	else
		echo "$PROC: fatal: cannot locate installation path" >&2
		exit 64
	fi
}

function main
{
	test  $# -ne 1 \
	  -o "$1" == "-h" \
	  -o "$1" == "-help" \
	  -o "$1" == "--help" \
	  && help

	case "$1" in
	"--cflags")
		setup
		echo "-I$INCDIR -x c"
		;;
	"--libs")
		setup
		echo "-L$LIBDIR -lsabotage"
		;;
	*)
		echo "$PROC: fatal: invalid option \`$1'" >&2
		exit 64
		;;
	esac
}

main "$@"
