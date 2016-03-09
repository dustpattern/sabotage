#
# Makefile
#
#    % make
#    % make install PREFIX=/usr/local
#

#
# The author has placed this work in the Public Domain, thereby
# relinquishing all copyrights. Everyone is free to use, modify,
# republish, sell or give away this work without prior consent from
# anybody.
#
# This software is provided on an "AS IS" basis, without warranty of any
# kind. Use at your own risk! Under no circumstances shall the author(s)
# or contributor(s) be liable for damages resulting directly or indirectly
# from the use or non-use of this documentation.
#

CC = gcc
AR = ar
CFLAGS = -std=gnu99 -Wall -Wextra -Werror -O2

.PHONY: all
all: sabotage sabotage-conf libsabotage.a
	$(MAKE) -f Makefile.sample

sabotage: sabotage.pl
sabotage-conf: sabotage-conf.sh
sabotage sabotage-conf:
	cat $(<) > $(@)
	chmod +x $(@)

sabotage.o: sabotage.c sabotage.h
	$(CC) $(CFLAGS) -c -o $(@) $< $(LFLAGS)

libsabotage.a: sabotage.o
	$(AR) cr $(@) $(^)

.PHONY: clean
clean:
	rm -f *.[ao]
	rm -fr *.dSYM
	rm -f sabotage
	rm -f sabotage-conf
	$(MAKE) -f Makefile.sample clean

TESTFILES = $(wildcard test/*.sh)
TESTS = ${TESTFILES:.sh=}
.PHONY: check $(TESTS)
check: $(TESTS)
$(TESTS): all
	bash $(@).sh | \
	  diff -q $(@).exp - >/dev/null && \
	  echo "$(@): Ok" || \
	  echo "$(@): Fail"

.PHONY: install
ifndef PREFIX
install: all
	$(error PREFIX is undefined)
else
install: all
	install -d "$(PREFIX)"
	install -d "$(PREFIX)/bin"
	install -d "$(PREFIX)/include"
	install -d "$(PREFIX)/share"
	install -d "$(PREFIX)/lib"
	install sabotage $(PREFIX)/bin
	install sabotage-conf $(PREFIX)/bin
	install -m 644 sabotage.h $(PREFIX)/include
	install -m 644 libsabotage.a $(PREFIX)/lib
endif
