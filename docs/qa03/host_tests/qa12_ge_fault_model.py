#!/usr/bin/env python3
"""QA12 host-only adversarial GE submission, present and memory-budget model.

Inspired by immutable perf06 rwsky.c, NOT a PSP emulator or a proposed patch.
The strict mode is a deliberately conservative fail-closed reference to test
safety invariants, not liveness, throughput, hardware cache or SDK correctness.
"""
from dataclasses import dataclass, field

@dataclass
class Job:
    seq: int
    bank: int
    ge_finished: bool = False
    display_accepted: bool = False

@dataclass
class Engine:
    strict: bool = False
    bank: int = 0
    next_seq: int = 0
    shown_seq: int = 0
    accepted_seq: int = 0
    watchdog_bypass: bool = False
    fault: str = ''
    jobs: dict = field(default_factory=dict)
    events: list = field(default_factory=list)

    def block(self, why):
        self.fault = why
        self.events.append('BLOCK:'+why)
        return False

    def start(self, gu_start_ok=True):
        if self.strict and self.fault:
            return False
        if self.strict and not gu_start_ok:
            return self.block('GU_START_FAILURE')
        if not gu_start_ok:
            self.events.append('UNACKNOWLEDGED_GU_START_FAILURE')
        return True

    def submit(self):
        if self.strict and self.fault:
            return False
        live = [j for j in self.jobs.values() if j.bank == self.bank and not j.ge_finished]
        if live:
            if self.strict:
                return self.block('BANK_LIVE')
            self.events.append('UNRETIRED_BANK_OVERWRITE')
        if self.jobs and not self.watchdog_bypass and self.shown_seq != self.next_seq:
            return False
        self.next_seq += 1
        self.jobs[self.next_seq] = Job(self.next_seq, self.bank)
        self.bank ^= 1
        self.watchdog_bypass = False
        self.events.append('SUBMIT:'+str(self.next_seq))
        return self.next_seq

    def ge_complete(self, seq):
        self.jobs[seq].ge_finished = True
        self.events.append('GE_DONE:'+str(seq))

    def display(self, seq, accepted):
        job = self.jobs[seq]
        if not job.ge_finished:
            return self.block('DISPLAY_BEFORE_GE_DONE')
        if accepted:
            job.display_accepted = True
            self.accepted_seq = seq
            self.shown_seq = seq
            self.events.append('ACCEPT:'+str(seq))
        elif self.strict:
            self.events.append('DISPLAY_REJECT:'+str(seq))
            return self.block('DISPLAY_REJECT')
        else:
            self.shown_seq = seq  # frozen source false ACK analog
            self.events.append('FALSE_SHOWN_ACK:'+str(seq))
        return True

    def watchdog(self, waited_ms):
        if waited_ms < 1000:
            self.events.append('TRACE_ONLY')
            return False
        if self.strict:
            return self.block('UNCONFIRMED_GE_RETIREMENT')
        self.watchdog_bypass = True
        self.events.append('WATCHDOG_CONTINUE')
        return True

    def serial_finish(self, status):
        # The real PSP API's return contract must be verified separately.
        # These synthetic values mean SUCCESS, ERROR, NOT_DONE for this model.
        if status == 'SUCCESS':
            self.events.append('SERIAL_RETIRED')
            return True
        if self.strict:
            return self.block('UNCONFIRMED_SERIAL_SYNC')
        self.events.append('UNCONFIRMED_SERIAL_REUSE')
        return True


def budget(request_bytes, current_words, capacity_words=65536, tail_words=2):
    """Bounded hypothetical getMemory layout: 2 command words + payload + tail."""
    if type(request_bytes) is not int or request_bytes < 0 or request_bytes > 2**31 - 4:
        return False
    aligned = (request_bytes + 3) & ~3
    if current_words < 0 or tail_words < 2:
        return False
    command_words = 2 + aligned // 4
    return command_words <= capacity_words - tail_words - current_words
