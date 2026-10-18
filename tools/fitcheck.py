#!/usr/bin/env python3
"""Reads a FIT file with the fitparse library and prints what it finds: a check of the writer against
an independent reader.

    python3 -m venv /tmp/qv && /tmp/qv/bin/pip install fitparse
    MAHLEBIKE_FIT_OUT=/tmp/x.fit tests/run.sh
    /tmp/qv/bin/python tools/fitcheck.py /tmp/x.fit
"""
import sys

from fitparse import FitFile

f = FitFile(sys.argv[1])
counts = {}
first = last = None
for m in f.get_messages():
    counts[m.name] = counts.get(m.name, 0) + 1
    if m.name == "record":
        first = first or m
        last = m
print("messages:", counts)
for m in f.get_messages("file_id"):
    print("file_id:", {d.name: d.value for d in m if d.value is not None})
for m in f.get_messages("session"):
    print("session:", {d.name: d.value for d in m if d.value is not None})
for m in f.get_messages("lap"):
    print("lap:", {d.name: d.value for d in m if d.value is not None})
print("first record:", {d.name: d.value for d in first if d.value is not None})
print("last record:", {d.name: d.value for d in last if d.value is not None})
