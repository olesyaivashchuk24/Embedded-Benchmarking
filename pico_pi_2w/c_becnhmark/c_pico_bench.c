#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"

// ========= PIN =========
#define PIN_TOGGLE 2
#define PIN_MARKER 4

// ========= CONFIG =========
#define ITER_GPIO 1000
#define MM_N 20
#define RUNS_PER_BENCH 5
#define BUSY_ITER 5000

#define FFT_N 256
#define FFT_RUNS 10
#define PI 3.14159265358979323846

#define WIFI_SSID "Olesya"
#define WIFI_PASS "olesa200524"

#define HTTP_HOST "httpbin.org"
#define HTTP_PORT 80
#define HTTP_PATH "/get"

#define HTTP_REQS 5

// ========= MARKER =========
static inline void marker_high(){ gpio_put(PIN_MARKER, 1); }
static inline void marker_low(){ gpio_put(PIN_MARKER, 0); }

// ========= TIMER =========
static inline uint64_t now_us(){
    return time_us_64();
}

// ========= GPIO INIT =========
void gpio_prep(){
    gpio_init(PIN_TOGGLE);
    gpio_set_dir(PIN_TOGGLE, GPIO_OUT);

    gpio_init(PIN_MARKER);
    gpio_set_dir(PIN_MARKER, GPIO_OUT);

    gpio_put(PIN_TOGGLE,0);
    gpio_put(PIN_MARKER,0);
}

// ========= P95 =========
int cmp_double(const void *a,const void *b){
    double da=*(double*)a;
    double db=*(double*)b;
    return (da>db)-(da<db);
}

double calc_p95(double *arr,int n){
    qsort(arr,n,sizeof(double),cmp_double);
    int idx=(int)(0.95*n);
    if(idx>=n) idx=n-1;
    return arr[idx];
}

// ========= GPIO BENCH =========
double bench_gpio_toggle(){

    int level=0;

    marker_high();
    uint64_t t0=now_us();

    for(int i=0;i<ITER_GPIO;i++){
        level^=1;
        gpio_put(PIN_TOGGLE,level);
    }

    uint64_t t1=now_us();
    marker_low();

    return (double)(t1-t0);
}

// ========= BUSY LOOP =========
double bench_busy_loop(){

    volatile uint32_t x=0;

    marker_high();
    uint64_t t0=now_us();

    for(uint32_t i=0;i<BUSY_ITER;i++){
        x = x * 1664525u + 1013904223u;
    }

    uint64_t t1=now_us();
    marker_low();

    return (double)(t1-t0);
}

// ========= MATRIX =========
void matmul_ijk(const int *A,const int *B,int *C,int n){

    for(int i=0;i<n;i++){
        for(int j=0;j<n;j++){

            int sum=0;

            for(int k=0;k<n;k++){
                sum += A[i*n+k]*B[k*n+j];
            }

            C[i*n+j]=sum;
        }
    }
}

double bench_matmul(){

    int *A=malloc(sizeof(int)*MM_N*MM_N);
    int *B=malloc(sizeof(int)*MM_N*MM_N);
    int *C=calloc(MM_N*MM_N,sizeof(int));

    for(int i=0;i<MM_N*MM_N;i++){
        A[i]=(i%7)+1;
        B[i]=(i%5)+1;
    }

    marker_high();
    uint64_t t0=now_us();

    matmul_ijk(A,B,C,MM_N);

    uint64_t t1=now_us();
    marker_low();

    free(A);
    free(B);
    free(C);

    return (double)(t1-t0);
}

// ========= FFT =========
void bit_reverse(double *re,double *im,int n){

    int j=0;

    for(int i=0;i<n;i++){

        if(i<j){
            double tr=re[i],ti=im[i];
            re[i]=re[j];
            im[i]=im[j];
            re[j]=tr;
            im[j]=ti;
        }

        int bit=n>>1;

        while(j & bit){
            j^=bit;
            bit>>=1;
        }

        j|=bit;
    }
}

void fft(double *re,double *im,int n){

    bit_reverse(re,im,n);

    for(int len=2;len<=n;len<<=1){

        double ang=-2.0*PI/len;

        double wlen_re=cos(ang);
        double wlen_im=sin(ang);

        for(int i=0;i<n;i+=len){

            double wre=1;
            double wim=0;

            for(int j=0;j<len/2;j++){

                int u=i+j;
                int v=i+j+len/2;

                double vr=re[v]*wre - im[v]*wim;
                double vi=re[v]*wim + im[v]*wre;

                re[v]=re[u]-vr;
                im[v]=im[u]-vi;

                re[u]+=vr;
                im[u]+=vi;

                double nwre=wre*wlen_re - wim*wlen_im;
                wim=wre*wlen_im + wim*wlen_re;
                wre=nwre;
            }
        }
    }
}

double bench_fft(){

    static double re[FFT_N];
    static double im[FFT_N];

    for(int i=0;i<FFT_N;i++){
        re[i]=sin(2*PI*i/FFT_N);
        im[i]=0;
    }

    marker_high();
    uint64_t t0=now_us();

    for(int r=0;r<FFT_RUNS;r++){

        double rtmp[FFT_N];
        double itmp[FFT_N];

        memcpy(rtmp,re,sizeof(re));
        memcpy(itmp,im,sizeof(im));

        fft(rtmp,itmp,FFT_N);
    }

    uint64_t t1=now_us();
    marker_low();

    return (double)(t1-t0)/FFT_RUNS;
}

// ========= HTTP LATENCY =========
double http_latency(){

    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return -1;

    ip_addr_t addr;
    err_t err = dns_gethostbyname(HTTP_HOST, &addr, NULL, NULL);

    if(err != ERR_OK){
        return -1;
    }

    uint64_t t0 = now_us();

    err = tcp_connect(pcb, &addr, HTTP_PORT, NULL);

    if(err != ERR_OK){
        return -1;
    }

    uint64_t t1 = now_us();

    tcp_close(pcb);

    return (double)(t1 - t0);
}

// ========= WIFI BENCH =========
double bench_wifi(){

    double sum = 0;

    for(int i=0;i<HTTP_REQS;i++){

        double t = http_latency();

        if(t > 0)
            sum += t;

        sleep_ms(200);
    }

    return sum / HTTP_REQS;
}

// ========= BENCH RUN =========
typedef double (*bench_fn)();

typedef struct{
    const char *name;
    bench_fn fn;
} bench_entry;

bench_entry BENCHES[]={
    {"busy_loop",bench_busy_loop},
    {"gpio_toggle",bench_gpio_toggle},
    {"matmul",bench_matmul},
    {"fft",bench_fft},
    {"wifi_latency",bench_wifi}
};

// ========= MAIN =========
int main(){

    stdio_init_all();

    // ждать подключения USB terminal
    sleep_ms(2000);

    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    printf("USB connected\n");

    if (cyw43_arch_init()) {
        printf("WiFi init failed\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");

    if (cyw43_arch_wifi_connect_timeout_ms(
            WIFI_SSID,
            WIFI_PASS,
            CYW43_AUTH_WPA2_AES_PSK,
            30000)) {

        printf("WiFi failed\n");
        return 1;
    }

    printf("WiFi connected\n");

    gpio_prep();

    printf("benchmark,mean_us,p95_us\n");

    for(int b=0;b<sizeof(BENCHES)/sizeof(BENCHES[0]);b++){

        double runs[RUNS_PER_BENCH];
        double sum=0;

        for(int r=0;r<RUNS_PER_BENCH;r++){

            runs[r]=BENCHES[b].fn();
            sum+=runs[r];

            sleep_ms(200);
        }

        printf("%s,%.2f,%.2f\n",
               BENCHES[b].name,
               sum/RUNS_PER_BENCH,
               calc_p95(runs,RUNS_PER_BENCH));
    }

    while(1){
        sleep_ms(1000);
    }
}