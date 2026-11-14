"""Execute the real audio worker and handoff code with controlled PSP calls."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class AudioHandoffTest(unittest.TestCase):
    def test_worker_and_acknowledgements(self):
        audio = (ROOT / 'port/audio.c').read_text()
        worker = audio[audio.index('static int audio_failure('):audio.index('int p4_audio_init(')]
        pause = audio[audio.index('int p4_audio_pause('):audio.index('int p4_audio_suspend_for_video(')]
        suspend = audio[audio.index('int p4_audio_suspend_for_video('):audio.index('void p4_audio_shutdown(')]
        source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef unsigned SceSize;
#define GRAIN 512
#define STREAMS 5
#define PSP_AUDIO_VOLUME_MAX 32768
#define BARRIER() ((void)0)
static int initialized,running,paused,pause_ack,output_reserved,sas;
static short output[2][GRAIN*2];
static unsigned p4_audio_step,p4_audio_errors,p4_audio_blocks,p4_audio_cpu_us;
static int p4_audio_last_error,requested[STREAMS];
static unsigned now,pause_mask,core_calls,delay_mode;
static int fail_core,fail_reserve,stuck_output,fail_pause,restored;
enum {EMPTY,LOAD,PREFILL,READY,PLAY,CLOSE,FAILED};
typedef struct {
    int state,fd,pending;
    unsigned id,pending_id,resume_block,source_samples,played;
    struct {unsigned blocks,loop;} header;
} Stream;
static Stream streams[STREAMS];
static int voices[32],sequences[16],suspended,unloads,module_error,worker_stopped;
static void audio_workers_stop(void){running=0;worker_stopped=1;memset(requested,0,sizeof requested);}
static int audio_modules_stop(void){assert(worker_stopped);++unloads;return module_error;}
static int audio_modules_start(void){assert(worker_stopped);return module_error;}
static int audio_workers_start(void){running=1;paused=pause_ack=0;worker_stopped=0;return 0;}
static unsigned long long runtime(void){return 0;}
static unsigned sceKernelGetSystemTimeLow(void){return now;}
static void p4_log(const char *fmt,...){(void)fmt;}
static void sceKernelDelayThread(unsigned us){
    now+=us;
    if(delay_mode==1)pause_ack=paused;
    /* Emulate the caller requesting resume after the paused worker yields. */
    if(delay_mode==2&&pause_ack)paused=0;
}
static int sceAudioOutput2GetRestSample(void){return stuck_output?512:0;}
static int sceAudioOutput2Release(void){assert(!pause_ack);return 0;}
static int sceAudioOutput2Reserve(int n){assert(n==GRAIN);return fail_reserve?-7:0;}
static int __sceSasGetPauseFlag(int *s){(void)s;return (int)0x80000005u;}
static int __sceSasSetPause(int *s,unsigned mask,int pause){
    (void)s;
    if(fail_pause)return -8;
    if(pause)pause_mask=mask;
    else {assert(mask==~0x80000005u);restored=1;}
    return 0;
}
static int __sceSasCore(int *s,void *buffer){
    (void)s;(void)buffer;++core_calls;
    if(p4_audio_step==3){assert(pause_mask==0xffffffffu);assert(!pause_ack);}
    if(fail_core)return -9;
    return 0;
}
static int sceAudioOutput2OutputBlocking(int v,void *b){
    (void)v;(void)b;assert(!pause_ack);running=0;return 0;
}
static void process_commands(void){}
static void stream_step(int i){(void)i;}
static void sequence_step(int i){(void)i;}
''' + worker + pause + suspend + r'''
static void reset(void){
    initialized=running=1;paused=pause_ack=output_reserved=0;
    now=pause_mask=core_calls=delay_mode=p4_audio_errors=p4_audio_blocks=0;
    fail_core=fail_reserve=stuck_output=fail_pause=restored=0;
    p4_audio_last_error=0;
    suspended=unloads=module_error=worker_stopped=0;
    memset(streams,0,sizeof streams);memset(voices,0,sizeof voices);memset(sequences,0,sizeof sequences);
}
int main(void){
    reset();initialized=0;assert(p4_audio_pause(1)==0);
    reset();delay_mode=1;assert(p4_audio_pause(1)==0&&pause_ack);
    /* Resume must wait for the worker, otherwise its old pause ACK can be reused. */
    assert(p4_audio_pause(0)==0&&!pause_ack&&now>=2000);
    reset();assert(p4_audio_pause(1)<0&&now==500000);
    reset();running=0;pause_ack=1;assert(p4_audio_pause(1)<0);
    reset();paused=output_reserved=1;delay_mode=2;
    audio_main(0,0);assert(core_calls==2&&restored&&p4_audio_blocks==1);
    assert(!pause_ack&&!p4_audio_errors);
    /* A failed silent grain must never publish a successful pause ACK. */
    reset();paused=output_reserved=1;fail_core=1;
    audio_main(0,0);assert(!pause_ack&&!running&&p4_audio_last_error==-9);
    assert(p4_audio_pause(1)<0);
    /* Preserve a looped stream's file block, not its ever-growing ring counter. */
    reset();delay_mode=1;
    streams[0].state=PLAY;streams[0].id=101;
    streams[0].header.blocks=1000;streams[0].header.loop=200;
    streams[0].source_samples=200*28;streams[0].played=2401*28;
    streams[1].state=PLAY;streams[1].id=102;
    streams[1].header.blocks=1000;streams[1].header.loop=0xffffffffu;streams[1].played=1000*28;
    streams[2].state=LOAD;streams[2].id=103;streams[2].pending=1;streams[2].pending_id=104;
    voices[0]=sequences[0]=1;
    assert(p4_audio_suspend_for_video()==1&&unloads==1&&suspended&&!running);
    assert(streams[0].state==EMPTY&&streams[0].pending_id==101&&streams[0].resume_block==201);
    assert(streams[0].fd==-1&&requested[0]);
    assert(!streams[1].pending&&!requested[1]);
    assert(streams[2].pending_id==104&&!streams[2].resume_block);
    assert(!voices[0]&&!sequences[0]);
    assert(p4_audio_resume_after_video()==0&&running&&!suspended);
    reset();initialized=0;assert(p4_audio_suspend_for_video()==0&&!unloads);
    reset();assert(p4_audio_suspend_for_video()<0&&!unloads&&!suspended);
    reset();delay_mode=1;module_error=-11;
    assert(p4_audio_suspend_for_video()<0&&suspended&&!running);
    assert(p4_audio_resume_after_video()<0&&p4_audio_last_error==-11&&!running);
    reset();paused=output_reserved=1;stuck_output=1;
    audio_main(0,0);assert(!pause_ack&&!running&&now<=1000000);
    reset();paused=1;fail_pause=1;
    audio_main(0,0);assert(!pause_ack&&!core_calls&&p4_audio_last_error==-8);
    reset();paused=pause_ack=1;delay_mode=2;fail_reserve=1;
    audio_main(0,0);assert(!running&&p4_audio_last_error==-7);
    assert(p4_audio_pause(1)<0);
    puts("audio handoff: worker ordering, resume, timeout and failure checks passed");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-handoff-test-') as folder:
            folder = Path(folder)
            c, exe = folder / 'check.c', folder / 'check.exe'
            c.write_text(source)
            cc = shutil.which('gcc') or shutil.which('clang')
            if cc:
                command = [cc, '-O2', str(c), '-o', str(exe)]
            else:
                vs = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio'
                compilers = sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Windows Kits/10'
                versions = sorted((kits / 'Include').glob('10.*'))
                if not compilers or not versions:
                    self.skipTest('host C compiler is unavailable')
                compiler, headers = compilers[-1], versions[-1]
                vc = compiler.parents[3]
                command = [str(compiler), '/nologo', '/std:c11', '/O2', f'/I{vc / "include"}',
                           f'/I{headers / "ucrt"}', str(c), f'/Fe:{exe}', f'/Fo:{folder / "check.obj"}',
                           '/link', f'/LIBPATH:{vc / "lib/x64"}',
                           f'/LIBPATH:{kits / "Lib" / headers.name / "ucrt/x64"}',
                           f'/LIBPATH:{kits / "Lib" / headers.name / "um/x64"}']
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
