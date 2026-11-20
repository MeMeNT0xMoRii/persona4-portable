#!/usr/bin/env python3
"""QA08 offline triage of normalized D1 status, not a PSP runtime parser.

Input JSON is an intentionally small normalized incident state (no saves/assets).
The label is a diagnostic *candidate*, never a demonstrated root cause.
"""
import argparse
import json
from pathlib import Path


def classify(d):
    def known(k):
        return k in d and d[k] is not None
    if d.get('manual_pause') or d.get('home_dialog') or d.get('standby'):
        primary = 'INTENTIONAL_PAUSE_OR_HOME'
    elif d.get('boot_data_failed'):
        primary = 'PRELOG_BOOT_OR_ASSET_FAILURE'
    elif d.get('bank_reused_before_ge_retired'):
        primary = 'P0_GE_BANK_LIFETIME_INVARIANT'
    elif (known('display_result') and d['display_result'] < 0
          and d.get('shown_seq_advanced')):
        primary = 'P0_FALSE_DISPLAY_ACK_INVARIANT'
    elif d.get('watchdog_gave_up') and d.get('ge_pending'):
        primary = 'P0_GE_WATCHDOG_PENDING'
    elif d.get('ge_pending') and d.get('game_tick_advanced'):
        primary = 'GE_QUEUE_BACKLOG_CANDIDATE'
    elif d.get('ge_finish_advanced') and not d.get('accepted_present_advanced'):
        primary = 'DISPLAY_FLIP_OR_VBLANK_CANDIDATE'
    elif d.get('io_in_flight') and d.get('io_wait_ms', 0) >= 20:
        primary = 'IO_WAIT_CORRELATION_CANDIDATE'
    elif d.get('game_tick_advanced') and d.get('frameskip') and not d.get('draw_advanced'):
        primary = 'EXPECTED_OR_EXCESSIVE_FRAMESKIP'
    elif d.get('cpu_frame_ms', 0) >= 50 and not d.get('ge_pending'):
        primary = 'CPU_TASK_OR_RENDER_BUILD_CANDIDATE'
    elif (d.get('heartbeat_known') and not d.get('game_tick_advanced')
          and not d.get('ge_finish_advanced')):
        primary = 'GAME_TASK_WAIT_OR_DEADLOCK_CANDIDATE'
    else:
        primary = 'UNKNOWN_NEEDS_MORE_CONTEXT'

    post = d.get('post_records')
    if not known('persist_ack') or not d['persist_ack']:
        durability = 'NOT_DURABLY_ACKED'
    else:
        durability = 'ACK_REPORTED_CHECK_CRC_READBACK'
    if post is None:
        incident = 'POST_COUNT_UNKNOWN'
    elif isinstance(post, int) and 0 <= post <= 127:
        incident = 'PARTIAL_STALL_0_TO_127_POST'
    elif post == 128:
        incident = 'FULL_128_POST'
    else:
        incident = 'INVALID_POST_COUNT'
    return {'primary_candidate': primary, 'durability': durability,
            'incident_completeness': incident,
            'confidence': 'UNCONFIRMED_HYPOTHESIS',
            'must_verify': ['source owner and actual linked ELF',
                            'matched cloned save/scene on PPSSPP or PSP Go',
                            'timelines and known intentional wait/skip',
                            'file readback/CRC before durable claim']}


def self_test():
    cases = [
        ('manual-home-not-crash', {'home_dialog': True, 'ge_pending': True}, 'INTENTIONAL_PAUSE_OR_HOME'),
        ('prelog-data-missing', {'boot_data_failed': True}, 'PRELOG_BOOT_OR_ASSET_FAILURE'),
        ('GE-bank-reused', {'bank_reused_before_ge_retired': True}, 'P0_GE_BANK_LIFETIME_INVARIANT'),
        ('bad-display-ack', {'display_result': -1, 'shown_seq_advanced': True}, 'P0_FALSE_DISPLAY_ACK_INVARIANT'),
        ('GE-watchdog', {'watchdog_gave_up': True, 'ge_pending': True}, 'P0_GE_WATCHDOG_PENDING'),
        ('GE-backlog', {'ge_pending': True, 'game_tick_advanced': True}, 'GE_QUEUE_BACKLOG_CANDIDATE'),
        ('finish-not-present', {'ge_finish_advanced': True, 'accepted_present_advanced': False}, 'DISPLAY_FLIP_OR_VBLANK_CANDIDATE'),
        ('IO-wait', {'io_in_flight': True, 'io_wait_ms': 80}, 'IO_WAIT_CORRELATION_CANDIDATE'),
        ('intentional-frameskip', {'game_tick_advanced': True, 'frameskip': True, 'draw_advanced': False}, 'EXPECTED_OR_EXCESSIVE_FRAMESKIP'),
        ('CPU-only', {'cpu_frame_ms': 80, 'ge_pending': False}, 'CPU_TASK_OR_RENDER_BUILD_CANDIDATE'),
        ('game-wait', {'heartbeat_known': True, 'game_tick_advanced': False, 'ge_finish_advanced': False}, 'GAME_TASK_WAIT_OR_DEADLOCK_CANDIDATE'),
        ('unknown', {'heartbeat_known': False}, 'UNKNOWN_NEEDS_MORE_CONTEXT'),
    ]
    for i, (name, entry, label) in enumerate(cases, 1):
        got = classify(entry)
        assert got['primary_candidate'] == label, (name, got)
        assert got['confidence'] == 'UNCONFIRMED_HYPOTHESIS'
        print(f'PASS {i:02d} {name}')
    assert classify({'post_records': 0})['incident_completeness'] == 'PARTIAL_STALL_0_TO_127_POST'
    assert classify({'post_records': 127})['incident_completeness'] == 'PARTIAL_STALL_0_TO_127_POST'
    assert classify({'post_records': 128})['incident_completeness'] == 'FULL_128_POST'
    assert classify({'post_records': 129})['incident_completeness'] == 'INVALID_POST_COUNT'
    assert classify({'persist_ack': False})['durability'] == 'NOT_DURABLY_ACKED'
    assert classify({'persist_ack': True})['durability'] == 'ACK_REPORTED_CHECK_CRC_READBACK'
    print('ALL 18/18 QA08 synthetic offline triage cases PASS; no real D1 record parsed.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test', action='store_true')
    p.add_argument('--input', type=Path, help='local normalized incident JSON; not raw PSP D1 packet')
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    if args.input is None:
        p.error('--input is required unless --self-test')
    d = json.loads(args.input.read_text(encoding='utf-8'))
    if not isinstance(d, dict):
        p.error('Input must be a JSON object')
    print(json.dumps(classify(d), indent=2))


if __name__ == '__main__':
    main()
