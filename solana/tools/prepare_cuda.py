#!/usr/bin/env python3
"""Mechanical CUDA adaptation of the vendored zlib-licensed orlp reference.
Adds device qualifiers, constant-memory tables, and an equivalent 16-word SHA512 schedule.
Generated files are marked altered and reside in the build directory.
"""
import re
import sys
from pathlib import Path
source, target = map(Path, sys.argv[1:])
target.mkdir(parents=True,exist_ok=True)
for name in ['fixedint.h','fe.h','fe.c','ge.h','ge.c','precomp_data.h','sha512.h','sha512.c']:
    s=(source/name).read_text()
    # Prototypes and definitions start at column 0; local variables do not.
    s=re.sub(r'^([ \t]*)(static )?(unsigned char|uint64_t|int|void)([ \t]+)(\w+)\s*\(',
             lambda m:m[1]+(m[2] or '')+'__device__ '+m[3]+m[4]+m[5]+'(',s,flags=re.M)
    s=s.replace('static const ge_precomp ', 'static __device__ __constant__ const ge_precomp ')
    s=s.replace('static const fe ', 'static __device__ __constant__ const fe ')
    s=s.replace('static const uint64_t K', 'static __device__ __constant__ const uint64_t K')
    if name == 'sha512.c':
        # Equivalent rolling message schedule: 16 words instead of 80 per thread.
        s=s.replace('W[80]', 'W[16]')
        start=s.index('    /* fill W[16..79] */')
        end=s.index('/* Compress */',start)
        s=s[:start]+s[end:]
        needle='    t0 = h + Sigma1(e)'
        schedule='    if ((i) >= 16) W[(i)&15] = Gamma1(W[((i)-2)&15]) + W[((i)-7)&15] + Gamma0(W[((i)-15)&15]) + W[(i)&15]; '
        s=s.replace(needle, schedule+chr(92)+'\n'+needle)
        s=s.replace('K[i] + W[i]', 'K[i] + W[(i)&15]')
    (target/name).write_text('// ALTERED: mechanical CUDA device adaptation; see tools/prepare_cuda.py.\n'+s)
