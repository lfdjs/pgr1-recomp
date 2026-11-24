#!/usr/bin/env python3
"""EN: Detect Linux x86-64 capabilities. PT-BR: Detecta capacidades x86-64 no Linux."""
import argparse
import hashlib
import os
import platform
from pathlib import Path

V2 = frozenset('lm cx16 lahf_lm popcnt pni ssse3 sse4_1 sse4_2'.split())
V3 = V2 | frozenset('avx avx2 bmi1 bmi2 f16c fma movbe xsave abm'.split())
V4 = V3 | frozenset('avx512f avx512bw avx512cd avx512dq avx512vl'.split())


def parse_cpuinfo(content, allowed=None):
    records = []
    for block in content.split('\n\n'):
        row = dict((key.strip(), value.strip()) for key, sep, value in
                   (line.partition(':') for line in block.splitlines()) if sep)
        if 'processor' in row and 'flags' in row:
            if allowed is None or int(row['processor']) in allowed:
                records.append(row)
    if not records:
        raise ValueError('No x86 CPU flags / Flags x86 indisponíveis')
    flags = set.intersection(*(set(row['flags'].split()) for row in records))
    if 'lzcnt' in flags:
        flags.add('abm')
    if 'sse3' in flags:
        flags.add('pni')
    return records, flags


def cpu_level(flags):
    for level, required in ((4, V4), (3, V3), (2, V2)):
        if required <= flags:
            return level
    return 1 if 'lm' in flags else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('auto','generic','v2','v3','v4','native'), default='auto')
    parser.add_argument('--guest-opt', choices=('1','2','3'), default='1')
    parser.add_argument('--signature', action='store_true')
    args = parser.parse_args()
    if platform.machine().lower() not in ('x86_64','amd64'):
        parser.error('Native game requires Linux x86-64 / O jogo requer Linux x86-64')
    try:
        content = Path('/proc/cpuinfo').read_text()
        # EN: Use all visible CPU flags, not just CPU 0. PT-BR: Use a interseção de todas as CPUs visíveis.
        all_records, flags = parse_cpuinfo(content)
        records, _ = parse_cpuinfo(content, os.sched_getaffinity(0))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    level = cpu_level(flags)
    required = {'v3': 3, 'v4': 4}.get(args.profile, 2)
    if level < required:
        parser.error(f'Profile {args.profile} requires x86-64-v{required}; host level/v{level}')
    if args.signature:
        # EN: Invalidate native objects if moved to a different CPU. PT-BR: Invalide objetos nativos ao trocar de CPU.
        identity = sorted({tuple(row.get(k, '') for k in ('vendor_id','cpu family','model','stepping','model name')) for row in all_records})
        print(hashlib.sha256(repr((identity, sorted(flags))).encode()).hexdigest())
        return
    cores = {(r['physical id'], r['core id']) for r in records if 'physical id' in r and 'core id' in r}
    known = all('physical id' in r and 'core id' in r for r in records)
    print('CPU / Processador: ' + ' | '.join(sorted({r.get('model name','unknown') for r in records})))
    print(f'Architecture / Arquitetura: x86-64-v{level}; SSE4.2={int("sse4_2" in flags)} AVX={int("avx" in flags)} AVX2={int("avx2" in flags)}')
    print(f'Logical CPUs available / Threads disponíveis: {len(records)}; physical cores / núcleos: {len(cores) if known else "unknown"}')
    print(f'Build profile / Perfil: {args.profile}; generated code / código recompilado: -O{args.guest_opt}')


if __name__ == '__main__':
    main()
