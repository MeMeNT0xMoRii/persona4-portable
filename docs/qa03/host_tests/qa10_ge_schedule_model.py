#!/usr/bin/env python3
"""QA10: small-source-inspired GE publication/flip/bank race schedule model.

Not a PSP emulator, executable or verified implementation. The 'safe' mode
is a proposed strict invariant protocol for negative test comparison only.
"""
import argparse
import copy
from collections import deque
from dataclasses import dataclass, field


@dataclass
class Job:
    seq: int
    bank: int
    fb: int
    finished: bool = False


@dataclass
class Model:
    mode: str = 'baseline'
    serial: int = 0
    bank: int = 0
    flip_seq: int = 0
    flip_fb: int = 0
    shown_seq: int = 0
    accepted_seq: int = 0
    finished_seq: int = 0
    watchdog_bypass: bool = False
    snap_seq: int = 0
    snap_fb: int = -1
    jobs: dict = field(default_factory=dict)
    violations: tuple = ()
    trace: tuple = ()

    def state_key(self):
        return (self.serial, self.bank, self.flip_seq, self.flip_fb,
                self.shown_seq, self.accepted_seq, self.finished_seq,
                self.watchdog_bypass, self.snap_seq, self.snap_fb,
                tuple((j.seq, j.bank, j.fb, j.finished) for j in self.jobs.values()),
                self.violations)

    def _fail(self, kind):
        if kind not in self.violations:
            self.violations += (kind,)

    def act(self, op):
        """One scheduler event; impossible/no-op actions pruned by returning False."""
        if op == 'submit':
            if self.serial >= 3:
                return False
            if self.serial and self.shown_seq != self.serial and not self.watchdog_bypass:
                return False
            if self.mode == 'safe' and self.serial and self.accepted_seq != self.serial:
                return False
            live = [x for x in self.jobs.values() if x.bank == self.bank and not x.finished]
            if live and self.mode == 'safe':
                return False
            if live:
                self._fail('UNRETIRED_GE_BANK_OVERWRITE')
            self.serial += 1
            # Both job identifiers are sample values, not actual PSP memory addresses.
            frame = Job(self.serial, self.bank, self.serial % 2)
            self.jobs[self.serial] = frame
            self.flip_fb = frame.fb             # frozen source publishes buffer then sequence
            self.flip_seq = self.serial
            self.bank ^= 1
            self.watchdog_bypass = False
        elif op == 'finish':
            if self.finished_seq >= self.serial:
                return False
            self.finished_seq += 1
            self.jobs[self.finished_seq].finished = True
        elif op == 'read_seq':
            if self.snap_seq or not self.flip_seq:
                return False
            if self.flip_seq == self.shown_seq or self.finished_seq < self.flip_seq:
                return False
            self.snap_seq = self.flip_seq
            self.snap_fb = -1
        elif op == 'read_fb':
            if not self.snap_seq or self.snap_fb != -1:
                return False
            if self.mode == 'safe':
                self.snap_fb = self.jobs[self.snap_seq].fb  # same committed record
            else:
                self.snap_fb = self.flip_fb                 # independent global read
            if self.snap_fb != self.jobs[self.snap_seq].fb:
                self._fail('TORN_SEQ_FRAMEBUFFER_PAIR')
        elif op in ('present_ok', 'present_fail'):
            if not self.snap_seq or self.snap_fb == -1:
                return False
            if op == 'present_ok':
                if self.snap_fb == self.jobs[self.snap_seq].fb:
                    self.accepted_seq = self.snap_seq
            elif self.mode == 'baseline':
                self._fail('DISPLAY_ACK_WITHOUT_SETFRAMEBUF_SUCCESS')
            if self.mode == 'baseline' or op == 'present_ok':
                # frozen: shown_seq advanced even if SetFrameBuf returned negative
                self.shown_seq = self.snap_seq
            self.snap_seq = 0
            self.snap_fb = -1
        elif op == 'watchdog':
            if not self.serial or self.shown_seq == self.serial or self.watchdog_bypass:
                return False
            if self.mode == 'safe':
                # Candidate policy never allows new bank reuse from an unfenced timeout.
                return False
            self.watchdog_bypass = True
        else:
            raise ValueError(op)
        self.trace += (op,)
        return True


OPS = ('submit', 'finish', 'read_seq', 'read_fb', 'present_ok', 'present_fail', 'watchdog')


def schedule(mode, ops):
    m = Model(mode=mode)
    for action in ops:
        if not m.act(action):
            raise AssertionError(f'impossible {mode} schedule at {action}: {m.trace}')
    return m


def enumerate_schedules(mode, max_depth=11, max_nodes=100000):
    """Bounded breadth-first state graph, not exhaustive real PSP timings."""
    q = deque([Model(mode=mode)])
    seen = {q[0].state_key()}
    witnesses = {}
    depth = 0
    while q and depth <= max_depth and len(seen) < max_nodes:
        for _ in range(len(q)):
            s = q.popleft()
            for v in s.violations:
                witnesses.setdefault(v, s.trace)
            if len(s.trace) == max_depth:
                continue
            for op in OPS:
                n = copy.deepcopy(s)
                if n.act(op):
                    key = n.state_key()
                    if key not in seen:
                        seen.add(key)
                        q.append(n)
        depth += 1
    return witnesses, len(seen), depth - 1, len(seen) >= max_nodes


def tests():
    count = 0
    def check(label, condition):
        nonlocal count
        assert condition, label
        count += 1
        print(f'PASS {count:02d}: {label}')

    p = schedule('baseline', ['submit', 'finish', 'read_seq', 'read_fb', 'present_fail'])
    check('frozen shown_seq advances despite failed display', p.shown_seq == 1 and p.accepted_seq == 0)
    check('failure counted as invalid display ACK', 'DISPLAY_ACK_WITHOUT_SETFRAMEBUF_SUCCESS' in p.violations)
    s = schedule('safe', ['submit', 'finish', 'read_seq', 'read_fb', 'present_fail'])
    check('proposed safe scheme does not ACK failed display', s.shown_seq == 0 and s.accepted_seq == 0 and not s.violations)
    q = schedule('baseline', ['submit', 'watchdog', 'submit', 'watchdog', 'submit'])
    check('baseline allows GE bank reuse when watchdog permits progress', 'UNRETIRED_GE_BANK_OVERWRITE' in q.violations)
    check('safe scheme rejects watchdog as override', not Model(mode='safe').act('watchdog'))
    k = schedule('baseline', ['submit', 'finish', 'read_seq', 'watchdog', 'submit', 'read_fb'])
    check('baseline permits torn seq/framebuffer in modeled interleaving', 'TORN_SEQ_FRAMEBUFFER_PAIR' in k.violations)
    j = schedule('safe', ['submit', 'finish', 'read_seq', 'read_fb', 'present_ok', 'submit', 'finish', 'read_seq', 'read_fb', 'present_ok'])
    check('safe matched successful two-frame commit', j.accepted_seq == 2 and j.shown_seq == 2 and not j.violations)
    check('safe refuses submission before accepted present', not schedule('safe', ['submit', 'finish']).act('submit'))
    check('actual GE finish and display success are distinct', j.finished_seq == j.accepted_seq and p.finished_seq != p.accepted_seq)
    check('GE bank retire independent of reused numeric qid',
          all(x.seq > 0 and x.bank in (0, 1) for x in j.jobs.values()))
    check('baseline mismatch not automatic across normal two-frame schedule',
          not schedule('baseline', ['submit', 'finish', 'read_seq', 'read_fb', 'present_ok', 'submit']).violations)
    check('failed display gives no accepted present in either model',
          p.accepted_seq == s.accepted_seq == 0)
    unsafe, count_base, d_base, cut_base = enumerate_schedules('baseline')
    check('bounded BFS finds all three independent counterexamples',
          {'DISPLAY_ACK_WITHOUT_SETFRAMEBUF_SUCCESS', 'UNRETIRED_GE_BANK_OVERWRITE', 'TORN_SEQ_FRAMEBUFFER_PAIR'} <= set(unsafe))
    safe, count_safe, d_safe, cut_safe = enumerate_schedules('safe')
    check('bounded safe candidate graph has no modeled safety violations', not safe)
    print(f'BFS baseline states={count_base}, max-explored-depth={d_base}, node-cap-hit={cut_base}')
    for name, trace in sorted(unsafe.items()):
        print(f'WITNESS {name}: {" → ".join(trace)}')
    print(f'BFS safe candidate states={count_safe}, max-explored-depth={d_safe}, node-cap-hit={cut_safe}')
    print(f'{count}/{count} synthetic GE schedule checks PASS; NO actual PSP concurrency proof')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        tests()
    else:
        ap.print_help()
