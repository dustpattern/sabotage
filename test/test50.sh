#!/usr/bin/env bash

D=$(dirname "$0")
PATH=$D/..:$PATH

export SABOTAGE="50% sample.c:foo(); 50% sample.c:bar(); 0%"
timeout 3 sample 2>&1 |
awk '/^sabotage: hit:/ { print $0 }' |
egrep -o 'line [[:digit:]]+' |
sort |
uniq
