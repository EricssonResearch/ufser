#!/usr/bin/env -S python3

""" This file implements a simple test for leaks in ufser 
    Prints all blocks leaked between the two snapshot calls.
    Experience is that you need to run serialize a few times before
    all leaks go away."""

import ufser
import tracemalloc

# These load all the static objects used by uf::serialize_append_guess()
ufser.serialize({1:1})
ufser.serialize([1])

tracemalloc.start(10)
s = tracemalloc.take_snapshot()
ufser.serialize([1,1])
s2 = tracemalloc.take_snapshot()
for a in s2.compare_to(s, 'lineno'):
    if 'lib/python3' not in str(a):
        print('1', a)
