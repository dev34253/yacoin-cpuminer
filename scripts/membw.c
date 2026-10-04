// T-08 memory probe (scripts/membw.c): sequential read bandwidth (mode 0) and
// random 128-byte chunk reads (mode 1). Build: gcc -O2 -mavx2 -pthread -o membw membw.c
// Run: ./membw THREADS MODE
// Memory probe: sequential read bandwidth and random 128-byte-chunk read
// throughput (16 independent chunk reads in flight per thread).
#include <immintrin.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
static size_t SZ = (size_t)2 << 30; static int NT; static int MODE; static double SECS = 4;
static uint8_t *buf; static volatile int stop;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
typedef struct {int id; double bytes; uint64_t sink;} arg_t;
static void *run(void *p){ arg_t *a=p; size_t part=SZ/NT; uint8_t *b=buf+part*a->id; __m256i acc=_mm256_setzero_si256(); double bytes=0;
 uint64_t s[16]; for(int i=0;i<16;i++) s[i]=0x9E3779B97F4A7C15ull*(i+1+a->id*16);
 size_t nchunks=SZ/128;
 while(!stop){
  if(MODE==0){ for(size_t o=0;o<part;o+=128){ acc=_mm256_xor_si256(acc,_mm256_load_si256((void*)(b+o))); acc=_mm256_xor_si256(acc,_mm256_load_si256((void*)(b+o+32)));acc=_mm256_xor_si256(acc,_mm256_load_si256((void*)(b+o+64)));acc=_mm256_xor_si256(acc,_mm256_load_si256((void*)(b+o+96)));} bytes+=part; }
  else { for(int r=0;r<4096;r++){ for(int i=0;i<16;i++){ s[i]=s[i]*6364136223846793005ull+1442695040888963407ull; uint8_t *c=buf+((s[i]>>20)%nchunks)*128; acc=_mm256_xor_si256(acc,_mm256_load_si256((void*)c)); acc=_mm256_xor_si256(acc,_mm256_load_si256((void*)(c+64)));} } bytes+=4096.0*16*128; }
 }
 a->bytes=bytes; a->sink=_mm256_extract_epi64(acc,0); return 0;}
int main(int c,char**v){ NT=atoi(v[1]); MODE=atoi(v[2]); buf=mmap(0,SZ,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); madvise(buf,SZ,MADV_HUGEPAGE); memset(buf,1,SZ);
 pthread_t t[16]; arg_t a[16]; double t0=now(); for(int i=0;i<NT;i++){a[i].id=i;pthread_create(&t[i],0,run,&a[i]);} 
 struct timespec d={(time_t)SECS,0}; nanosleep(&d,0); stop=1; double tot=0; for(int i=0;i<NT;i++){pthread_join(t[i],0);tot+=a[i].bytes;} double el=now()-t0;
 printf("threads=%d mode=%s GB/s=%.2f chunks/s=%.3g\n",NT,MODE?"rand128":"seq",tot/el/1e9,tot/el/128); return 0;}
