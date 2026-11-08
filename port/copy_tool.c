/* Bulk deployment helper. Large reads avoid PSPLink shell cp's 2KB round trips.
 * Run only after stopping the game. Verify every destination from the card. */
#include <pspkernel.h>
#include <psputils.h>
#include <psppower.h>
#include <pspdebug.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
PSP_MODULE_INFO("P4Deploy",0,1,0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(64);
PSP_HEAP_SIZE_KB(1024);
static unsigned char buffer[64*1024] __attribute__((aligned(64)));
static int logfd=-1;
static void report(const char *format,...)
{
    char text[768];va_list ap;va_start(ap,format);vsnprintf(text,sizeof text,format,ap);va_end(ap);
    if(logfd>=0)sceIoWrite(logfd,text,strlen(text));
    printf("%s",text);fflush(stdout);pspDebugScreenPrintf("%s",text);
}
static int hash_file(const char *path,unsigned long long expected,unsigned char digest[20])
{
    int fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return -1;
    SceKernelUtilsSha1Context context;sceKernelUtilsSha1BlockInit(&context);
    unsigned long long total=0;int n;
    while((n=sceIoRead(fd,buffer,sizeof buffer))>0){sceKernelUtilsSha1BlockUpdate(&context,buffer,n);total+=n;}
    sceIoClose(fd);
    if(n<0||total!=expected)return -1;
    return sceKernelUtilsSha1BlockResult(&context,digest);
}
static int copy_file(const char *source,const char *target,unsigned long long size,const unsigned char expected[20])
{
    SceIoStat stat;unsigned char digest[20];
    if(sceIoGetstat(target,&stat)>=0&&(unsigned long long)stat.st_size==size&&hash_file(target,size,digest)>=0&&!memcmp(digest,expected,20)){
        report("SKIP verified %s\n",target);return 0;
    }
    int in=sceIoOpen(source,PSP_O_RDONLY,0);if(in<0){report("ERROR open source %08x %s\n",in,source);return -1;}
    int out=sceIoOpen(target,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    if(out<0){sceIoClose(in);report("ERROR open target %08x %s\n",out,target);return -1;}
    SceKernelUtilsSha1Context context;sceKernelUtilsSha1BlockInit(&context);
    unsigned long long total=0,next_report=16*1024*1024;int n,result=0;
    while((n=sceIoRead(in,buffer,sizeof buffer))>0){
        int offset=0;
        while(offset<n){int written=sceIoWrite(out,buffer+offset,n-offset);if(written<=0){result=-1;break;}offset+=written;}
        if(result<0)break;
        sceKernelUtilsSha1BlockUpdate(&context,buffer,n);total+=n;
        if(total>=next_report){report("COPY %s %llu/%llu MiB\n",target,total>>20,size>>20);next_report+=16*1024*1024;}
    }
    sceIoClose(in);sceIoClose(out);sceIoSync("ms0:",0);
    sceKernelUtilsSha1BlockResult(&context,digest);
    if(n<0||result<0||total!=size||memcmp(digest,expected,20)){report("ERROR copy/checksum %s bytes=%llu expected=%llu\n",target,total,size);return -1;}
    report("READBACK %s\n",target);
    if(hash_file(target,size,digest)<0||memcmp(digest,expected,20)){report("ERROR card readback %s\n",target);return -1;}
    report("VERIFIED %s bytes=%llu\n",target,size);return 0;
}
int main(int argc,char **argv)
{
    pspDebugScreenInit();scePowerSetClockFrequency(333,333,166);
    logfd=sceIoOpen("ms0:/PSP/GAME/P4/deploy.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    int result=-1,files=0;
    FILE *list=argc>1?fopen(argv[1],"r"):NULL;
    if(!list){report("ERROR deployment manifest\n");goto done;}
    char line[640],source[256],target[256],hex[41];unsigned long long size;
    result=0;
    while(fgets(line,sizeof line,list)){
        unsigned char digest[20];
        if(sscanf(line,"%40s %llu %255s %255s",hex,&size,source,target)!=4||strlen(hex)!=40||
           strncmp(source,"host0:/",7)||strncmp(target,"ms0:/PSP/GAME/P4/",17)||strstr(target,"..")){
            report("ERROR invalid manifest line\n");result=-1;break;
        }
        for(int i=0;i<20;++i){unsigned byte;if(sscanf(hex+i*2,"%2x",&byte)!=1){result=-1;break;}digest[i]=byte;}
        if(result<0||copy_file(source,target,size,digest)<0){result=-1;break;}
        ++files;
    }
    fclose(list);
done:
    report("DEPLOY DONE result=%d files=%d\n",result,files);
    if(logfd>=0)sceIoClose(logfd);
    sceKernelExitDeleteThread(result);return result;
}
