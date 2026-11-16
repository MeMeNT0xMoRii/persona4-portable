"""Run real video cleanup with firmware retaining ownership until Finish."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class VideoLifecycleTest(unittest.TestCase):
    def test_firmware_finishes_before_buffers_are_released(self):
        video = (ROOT/'port/video.c').read_text()
        shutdown = re.search(r'^void p4_video_shutdown\(void\)\n\{.*?^\}', video, re.M|re.S)[0]
        cleanup = video[video.index('    if (kept && result < 0)'):video.index('    log_line("probe finished')]
        source = r'''
#include <assert.h>
#include <stdarg.h>
#include <string.h>
typedef void *SceMpeg;
enum {PSP_AV_MODULE_MPEGBASE=1,PSP_AV_MODULE_AVCODEC=2};
static int kept,p4_video_keep,initialized,base_loaded,codec_loaded,vol_held,rb;
static SceMpeg kept_mpeg;
static unsigned last_movie_end;
static int calls[64],count,live,freed,fail_base;
enum {DELETE=10,RING,FINISH,BASE,CODEC,UNLOCK,FREE,CLOSE};
static void log_line(const char *fmt,...){(void)fmt;}
static void sceMpegDelete(SceMpeg *m){assert(live&&initialized&&!freed&&*m);live=0;calls[count++]=DELETE;}
static void sceMpegRingbufferDestruct(int *r){assert(r==&rb&&!live&&!freed);calls[count++]=RING;}
static void sceMpegFinish(void){assert(!live&&initialized&&!freed);calls[count++]=FINISH;}
static int sceUtilityUnloadAvModule(int id){assert(!initialized&&!freed);calls[count++]=id==1?BASE:CODEC;return id==1&&fail_base?-1:0;}
static int sceKernelVolatileMemUnlock(int id){assert(!id&&!live&&!initialized&&!freed);calls[count++]=UNLOCK;return 0;}
static unsigned sceKernelGetSystemTimeLow(void){return 123;}
static void sceIoClose(int fd){assert(fd==7);calls[count++]=CLOSE;}
static void tracked_free(void *p){(void)p;assert(!live&&!initialized);freed++;calls[count++]=FREE;}
#define free tracked_free
''' + shutdown + r'''
static void cleanup(int result,int created,int rb_created,int heap_work,int volatile_locked){
    SceMpeg mpeg=kept_mpeg;void *ddr=(void*)1,*work=(void*)2,*output=(void*)3,*packet=(void*)4,*params=(void*)5;int fd=7;
''' + cleanup + r'''
}
static void reset(void){
    count=freed=fail_base=kept=p4_video_keep=vol_held=0;
    live=initialized=base_loaded=codec_loaded=1;kept_mpeg=(void*)8;
}
static void prefix(void){assert(calls[0]==DELETE&&calls[1]==RING&&calls[2]==FINISH&&calls[3]==BASE&&calls[4]==CODEC);}
int main(void){
    reset();cleanup(0,1,1,1,0);prefix();assert(freed==5&&!initialized&&!base_loaded&&!codec_loaded);
    reset();vol_held=1;cleanup(0,1,1,0,1);prefix();assert(calls[5]==UNLOCK&&!vol_held&&!freed);
    reset();vol_held=kept=p4_video_keep=1;cleanup(0,1,1,0,1);
    assert(initialized&&kept&&live&&vol_held&&!freed&&count==1&&calls[0]==CLOSE);
    count=0;p4_video_shutdown();prefix();assert(calls[5]==UNLOCK&&!kept&&!vol_held);
    reset();vol_held=kept=p4_video_keep=1;cleanup(-1,1,1,0,1);prefix();assert(!kept&&!vol_held&&!freed);
    reset();live=0;codec_loaded=2;p4_video_shutdown();assert(count==2&&calls[0]==FINISH&&calls[1]==BASE&&!codec_loaded);
    reset();live=0;fail_base=1;p4_video_shutdown();assert(base_loaded==1&&codec_loaded==1&&!initialized);
    fail_base=0;count=0;p4_video_shutdown();assert(count==2&&calls[0]==BASE&&calls[1]==CODEC&&!base_loaded&&!codec_loaded);
    reset();live=initialized=base_loaded=codec_loaded=0;cleanup(-1,0,0,0,0);assert(freed==5);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-video-close-') as name:
            folder=Path(name);c=folder/'check.c';exe=folder/'check.exe';c.write_text(source)
            cc=shutil.which('gcc') or shutil.which('clang')
            if cc:
                command=[cc,'-std=c11','-O2',f'-I{ROOT / "port"}',str(c),'-o',str(exe)]
            else:
                vs=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio'
                compilers=sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10'
                headers=sorted((kits/'Include').glob('10.*'))
                if not compilers or not headers:self.skipTest('host C compiler unavailable')
                compiler,inc=compilers[-1],headers[-1];vc=compiler.parents[3]
                command=[str(compiler),'/nologo','/std:c11','/O2',f'/I{vc / "include"}',f'/I{inc / "ucrt"}',
                         str(c),f'/Fe:{exe}',f'/Fo:{folder / "check.obj"}','/link',f'/LIBPATH:{vc / "lib/x64"}',
                         f'/LIBPATH:{kits / "Lib" / inc.name / "ucrt/x64"}',f'/LIBPATH:{kits / "Lib" / inc.name / "um/x64"}']
            result=subprocess.run(command,cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
