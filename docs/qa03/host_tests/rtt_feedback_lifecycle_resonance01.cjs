#!/usr/bin/env node
// QA06 RESONANCE01: static H0 model of frozen perf06 RTT allocator/feedback state.
// Source pinned: MeMeNT0xMoRii/persona4-portable@29b0fcfdec70222a4052099b67fb4a11e8d8aff4
// port/rwsky.c:22-24,1202-1273,1318-1328,2536-2542,2655-2668.
// This is NOT a PSP executable, VRAM trace, or benchmark.
"use strict";
const assert = require("node:assert/strict");
const { SCREEN_W: W, SCREEN_H: H, STRIDE, VRAM_END, ALIGN, RTT_SLOTS } = {
  SCREEN_W: 480, SCREEN_H: 272, STRIDE: 512, VRAM_END: 0x200000,
  ALIGN: 0x2000, RTT_SLOTS: 4,
};
const FULL_BASE = 2*STRIDE*H*4 + STRIDE*H*2;
const TAIL = (FULL_BASE + STRIDE*(H+1)*4 + ALIGN-1) & ~(ALIGN-1);
let checks = 0;
function check(name, actual, expected) {
  assert.deepEqual(actual, expected, name);
  checks++;
  process.stdout.write("PASS "+String(checks).padStart(2)+" "+name+"\n");
}
class RttModel {
  constructor() { this.enabled=1; this.mode=0; this.feedback=1; this.slots=[]; }
  size(n) { let s=8; while(s<n && s<512) s<<=1; return s; }
  find(id) { return this.slots.find(s=>s.id===id)||null; }
  place(base, bytes) {
    const candidates=[base,...this.slots.filter(s=>s.at+s.bytes>=base)
      .map(s=>(s.at+s.bytes+ALIGN-1)&~(ALIGN-1))];
    for (const at of candidates) {
      if (at+bytes>VRAM_END) continue;
      if (this.slots.some(s=>at<s.at+s.bytes&&s.at<at+bytes)) continue;
      return at;
    }
    return null;
  }
  get({id,w,h,depth,type=5}) {
    const old=this.find(id);
    if(old||!this.enabled||type!==5) return old;
    if(this.slots.length>=RTT_SLOTS)return null;
    const fw=this.size(w),fh=this.size(h),bpp=depth===16?2:4;
    for(let scale=this.mode===2?2:1;scale<=2;scale++) {
      const sw=fw/scale,sh=fh/scale,bytes=sw*sh*bpp;
      const base=scale===1&&this.mode===1?FULL_BASE:TAIL;
      const at=this.place(base,bytes);
      if(at===null)continue;
      if(base===FULL_BASE)this.feedback=0; // no matching reset in raster destroy
      const slot={id,at,bytes,w:sw,h:sh,scale};
      this.slots.push(slot);
      return slot;
    }
    return null;
  }
  destroy(id) { this.slots=this.slots.filter(s=>s.id!==id); }
}
check("480x272 source raster", [W,H,STRIDE], [480,272,512]);
check("full VRAM base",FULL_BASE,1392640);
check("2MiB VRAM",VRAM_END,2097152);
check("8KiB-aligned tail",TAIL%ALIGN,0);
check("tail bytes",VRAM_END-TAIL,139264);
const m=new RttModel();
check("128x128x32 default full allocation",m.get({id:1,w:128,h:128,depth:32}).scale,1);
check("small RTT preserves VRAM feedback",m.feedback,1);
check("512x256x16 default halves",m.get({id:2,w:512,h:256,depth:16}).scale,2);
check("two existing slots bytes",m.slots.reduce((a,b)=>a+b.bytes,0),131072);
check("third 128x128x32 cannot fit",m.get({id:3,w:128,h:128,depth:32}),null);
m.enabled=0;
check("disabling RTT retains existing slot",m.get({id:1,w:128,h:128,depth:32}).id,1);
check("disabling RTT blocks new slot",m.get({id:4,w:64,h:64,depth:32}),null);
m.enabled=1; m.mode=2;
check("RTT scale toggle retains previous slot",m.get({id:1,w:128,h:128,depth:32}).scale,1);
const n=new RttModel(); n.mode=1;
const big=n.get({id:10,w:512,h:256,depth:16});
check("forced full allocated at copy region",big.at,FULL_BASE);
check("forced full bytes",big.bytes,262144);
check("forced full switches feedback RAM",n.feedback,0);
n.destroy(10);
check("destroy frees large slot",n.slots.length,0);
check("destroy does not restore feedback",n.feedback,0);
n.mode=0;
n.get({id:11,w:128,h:128,depth:32});
check("later small RTT still leaves RAM feedback",n.feedback,0);
check("default full large RTT exceeds tail",512*256*2>VRAM_END-TAIL,true);
check("full copy reserved bytes",STRIDE*(H+1)*4,559104);
check("power-of-two size clamps at 512",m.size(300),512);
console.log("H0:",checks+"/"+checks,"PASSED; no game assets; no PSP/PPSSPP execution");
