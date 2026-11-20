#!/usr/bin/env python3
"""Source-extracted QA10 inspect/map path collision probe (not PSP ELF)."""
import re
from dataclasses import dataclass
from pathlib import Path

@dataclass(frozen=True)
class Definition:
    name:str
    addr:int
    kind:str
    owner:str

STRONG=set('TDBRSG')
MAP_SECTION=re.compile(r'^\s*(\.text(?:\.[\w.$-]+)?)\s+(0x[\da-fA-F]+)\s+(0x[\da-fA-F]+)\s+(.+?\.o)(?:\s.*)?$')
MAP_SYMBOL=re.compile(r'^\s*(0x[\da-fA-F]+)\s+(\S+)\s*$')

def parse_map(s):
    ranges=[]; symbols={}
    for line in s.splitlines():
        m=MAP_SECTION.match(line)
        if m:
            _, addr, size, owner=m.groups()
            lo,n=int(addr,16),int(size,16)
            if n:ranges.append((lo,lo+n,owner.strip().replace('\\','/')))
        m=MAP_SYMBOL.match(line)
        if m:
            addr,name=m.groups()
            if re.fullmatch(r'[A-Za-z_][\w.$@]*',name):symbols.setdefault(name,[]).append(int(addr,16))
    return ranges,symbols

def old_decision(name, expected, elf, obj, maptext):
    address=elf[name][0][0]
    defs=obj[name]
    expected_matches=[d for d in defs if Path(d.owner).name == Path(expected).name]
    sections,symbols=parse_map(maptext)
    owners={owner for lo,hi,owner in sections if lo<=address<hi}
    if address not in symbols.get(name,[]):return 'INCONCLUSIVE'
    if len(owners)!=1:return 'INCONCLUSIVE'
    map_owner=next(iter(owners))
    if Path(map_owner).name!=Path(expected).name:return 'BLOCKED'
    return 'PASS'  # QA10 also appends unrelated machine-delta INCONCLUSIVE

def improved_decision(name, expected, elf, obj, maptext):
    address=elf[name][0][0]
    defs=obj[name]
    expected_matches=[d for d in defs if Path(d.owner).name == Path(expected).name]
    sections,symbols=parse_map(maptext)
    owners={owner for lo,hi,owner in sections if lo<=address<hi}
    if address not in symbols.get(name,[]):return 'INCONCLUSIVE'
    if len(owners)!=1:return 'INCONCLUSIVE'
    map_owner=next(iter(owners))
    if Path(map_owner).name!=Path(expected).name:return 'BLOCKED'
    if map_owner not in {d.owner for d in expected_matches}:
        return 'INCONCLUSIVE'  # copy or same basename from different directory; require provenance
    return 'PASS'

def require(v, label):
    if not v: raise RuntimeError(label)

def main():
    name='func_003f79b0'
    elf={name:[(0x4001000,'T')]}
    obj={name:[Definition(name,0x10,'T','C:/build/candidate/rwsky.o')]}
    good='.text 0x04000f00 0x200 C:/build/candidate/rwsky.o\n 0x04001000 func_003f79b0'
    wrong='.text 0x04000f00 0x200 C:/build/stale/rwsky.o\n 0x04001000 func_003f79b0'
    require(old_decision(name,'rwsky.o',elf,obj,wrong)=='PASS', 'old basename collision')
    require(improved_decision(name,'rwsky.o',elf,obj,wrong)=='INCONCLUSIVE', 'new uncertainty')
    require(improved_decision(name,'rwsky.o',elf,obj,good)=='PASS', 'good matching path')
    print('3/3 source-extracted owner path collision negative checks PASS: stale same-basename must not be candidate PASS')
if __name__=='__main__':main()
