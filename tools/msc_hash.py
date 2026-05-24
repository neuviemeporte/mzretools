#!/usr/bin/env python3
"""
MSC 5.1 local variable name hash tool.

MSC 5.1 uses a 16-bucket hash table for local variable symbols.
  hash(name) = sum(ord(c.upper()) for c in name) % 16

The hash table uses chaining (not open addressing). Variables are
appended to their bucket's chain in declaration order. During stack
allocation, buckets are iterated 0..15 and each chain is walked in
LIFO order (last declared variable in that bucket gets allocated first).

This means:
- Variables hashing to different buckets are ordered by bucket number.
- Variables hashing to the same bucket are ordered in reverse declaration
  order (last declared → lowest BP offset).

Usage:
  python3 msc_hash.py <name>              compute hash for a single name
  python3 msc_hash.py <name1> <name2> ... show allocation order for given names
"""

import sys
from itertools import product
import string

TABLE_SIZE = 16

def msc_hash(name):
    """Compute MSC 5.1 local variable name hash."""
    return sum(ord(c.upper()) for c in name) % TABLE_SIZE

def allocate_vars(names, sizes=None):
    """Simulate MSC 5.1 hash table insertion and return allocation order.

    Args:
        names: list of variable names in declaration order
        sizes: optional list of sizes in bytes (default: 2 for each)

    Returns list of (name, bp_offset, size) tuples in allocation order.
    """
    if sizes is None:
        sizes = [2] * len(names)

    # Insert into chained hash buckets (append = declaration order)
    buckets = [[] for _ in range(TABLE_SIZE)]
    for i, name in enumerate(names):
        h = msc_hash(name)
        buckets[h].append((name, sizes[i]))

    # Allocate: iterate buckets 0..15, each chain in LIFO order
    result = []
    bp_offset = 2
    for bucket_idx in range(TABLE_SIZE):
        for name, size in reversed(buckets[bucket_idx]):
            result.append((name, bp_offset, size))
            bp_offset += size

    return result

def show_allocation(names, sizes=None):
    """Display the allocation for a set of variable names."""
    alloc = allocate_vars(names, sizes)
    print(f"{'Name':<20} {'Hash':>4} {'Bucket':>6}  {'BP offset':>10}")
    print("-" * 46)
    for name, offset, size in alloc:
        h = msc_hash(name)
        print(f"{name:<20} {h:>4} {h:>6}  bp-0x{offset:02x}")

def find_names_for_target(target_positions, var_sizes, max_name_len=6):
    """
    Given target bucket positions (the desired allocation order),
    find variable names that hash to produce that ordering.

    target_positions: list of target hash bucket values for each variable
    var_sizes: list of variable sizes (not used in hash, just for reference)
    """
    # For each target bucket, find candidate names
    charset = string.ascii_lowercase + '_'

    for target_bucket in sorted(set(target_positions)):
        print(f"\nNames that hash to bucket {target_bucket}:")
        candidates = []
        # Try 1-char names
        for c in charset:
            if msc_hash(c) == target_bucket:
                candidates.append(c)
        # Try 2-char names
        for c1, c2 in product(charset, repeat=2):
            name = c1 + c2
            if msc_hash(name) == target_bucket:
                candidates.append(name)
        # Show first 20
        print(f"  {candidates[:20]}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    if len(sys.argv) == 2:
        name = sys.argv[1]
        h = msc_hash(name)
        print(f"hash('{name}') = {h} (bucket {h} of 16)")
    else:
        names = sys.argv[1:]
        show_allocation(names)
