#!/usr/bin/env bash

D=$(dirname "$0")
PATH=$D/..:$PATH

export SABOTAGE="100%"
timeout 1 sample 2>&1 |
awk '/^sabotage: hit:/ { print $0 }' |
uniq
