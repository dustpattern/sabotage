#!/usr/bin/env bash

D=$(dirname "$0")
PATH=$D/..:$PATH

export SABOTAGE="25%"
timeout 5 sample 2>&1 |
awk '/^sabotage: hit:/ { print $0 }' |
egrep -o 'line [[:digit:]]+' |
sort |
uniq
