#!/usr/bin/env python
"""Summarize complete on-device SAS logs; never infer timing from an emulator."""
import argparse
import json
from pathlib import Path


def read_log(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    if 'DONE result=0' not in text or 'ERROR ' in text:
        raise ValueError(f'{path}: incomplete or failed run')
    rows = []
    for line in text.splitlines():
        if not line.startswith('RESULT '):
            continue
        row = dict(item.split('=', 1) for item in line.split()[1:])
        for key in row:
            if key in ('format', 'backend'):
                continue
            row[key] = int(row[key], 16) if key == 'end_flags' else float(row[key])
        if row['busy_us'] < 0 or not 0 <= row['thread_us'] <= row['wall_us'] + 1000:
            raise ValueError(f'{path}: invalid runtime counters')
        if row['sas']:
            expected = (~((1 << int(row['voices'])) - 1)) & 0xffffffff
            if row['end_flags'] != expected:
                raise ValueError(f'{path}: voices stopped unexpectedly')
        rows.append(row)
    if len(rows) != 20:
        raise ValueError(f'{path}: expected 20 measurement cases, got {len(rows)}')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    results = {}
    for path in args.logs:
        rows = read_log(path)
        results[str(path)] = rows
        print(f'\n{path.name}')
        print('format voices reverb thread_ms/tick total_busy_ms/tick net_busy_ms/tick peak_without_monitor_ms')
        for row in rows:
            if not row['sas']:
                continue
            peak = row.get('peak_without_monitor_us')
            peak_text = '-' if peak is None else f'{peak / 1000:.3f}'
            if 'samples' in row and 'monitor_cpu_us' not in row:
                net_text = '-'  # Older monitor logs did not record its total runtime.
            else:
                monitor = row.get('monitor_cpu_us', 0)
                net = (row['busy_us'] - monitor) * 1000 / (30 * row['wall_us'])
                net_text = f'{net:.3f}'
            print(f"{row['format']:6} {int(row['voices']):6} {int(row['reverb']):6} "
                  f"{row['thread_us_per_tick'] / 1000:14.3f} "
                  f"{row['busy_us_per_tick'] / 1000:18.3f} {net_text:>16} {peak_text}")
    if args.json:
        args.json.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
