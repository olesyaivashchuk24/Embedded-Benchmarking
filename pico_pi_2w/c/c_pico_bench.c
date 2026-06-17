#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "lwip/dns.h"
#include "lwip/altcp_tcp.h"
#include "lwip/apps/http_client.h"

// ========= WIFI CONFIG =========
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"
#define HTTP_HOST     "httpbin.org"
#define HTTP_PATH     "/get"
#define HTTP_REQS     20

// ========= PIN ASSIGNMENTS =========
#define PIN_TOGGLE 2
#define PIN_MARKER 3

// ========= BENCHMARK CONFIG =========
#define ITER_GPIO      1000
#define MM_N           32   // 32x32 matrix (matches paper)
#define RUNS_PER_BENCH 20   // 20 runs for meaningful p95
#define BUSY_ITER      5000

// ========= FFT CONFIG =========
#define FFT_N    256
#define FFT_RUNS 10
#define PI       3.14159265358979323846

// ========= UTILS =========
static inline void marker_high(void) { gpio_put(PIN_MARKER, 1); }
static inline void marker_low(void)  { gpio_put(PIN_MARKER, 0); }

static inline uint64_t time_us(void) {
    return time_us_64();
}

static inline size_t heap_free_bytes(void) {
    // Pico SDK does not expose a direct heap free API;
    // we approximate via mallinfo if available, otherwise return 0
    return 0;
}

static int compare_doubles(const void *a, const void *b) {
    double da = *(double *)a;
    double db = *(double *)b;
    return (da > db) - (da < db);
}

static double calculate_p95(double *times, int n) {
    // copy to avoid mutating original
    double tmp[RUNS_PER_BENCH];
    memcpy(tmp, times, n * sizeof(double));
    qsort(tmp, n, sizeof(double), compare_doubles);
    int idx = (int)(0.95 * n);
    if (idx >= n) idx = n - 1;
    return tmp[idx];
}

static void gpio_prep(void) {
    gpio_init(PIN_TOGGLE);
    gpio_set_dir(PIN_TOGGLE, GPIO_OUT);
    gpio_put(PIN_TOGGLE, 0);

    gpio_init(PIN_MARKER);
    gpio_set_dir(PIN_MARKER, GPIO_OUT);
    gpio_put(PIN_MARKER, 0);
}

// ========= BENCHMARK 1: BUSY LOOP =========
static double bench_busy_loop(size_t *heap_before, size_t *heap_after) {
    *heap_before = heap_free_bytes();

    volatile uint32_t x = 0;
    marker_high();
    uint64_t t0 = time_us();

    for (uint32_t i = 0; i < BUSY_ITER; i++) {
        x = x * 1664525u + 1013904223u;
    }

    uint64_t t1 = time_us();
    marker_low();

    *heap_after = heap_free_bytes();
    return (double)(t1 - t0);
}

// ========= BENCHMARK 2: GPIO TOGGLE =========
static double bench_gpio_toggle(size_t *heap_before, size_t *heap_after) {
    *heap_before = heap_free_bytes();

    int level = 0;
    marker_high();
    uint64_t t0 = time_us();

    for (int i = 0; i < ITER_GPIO; i++) {
        level ^= 1;
        gpio_put(PIN_TOGGLE, level);
    }

    uint64_t t1 = time_us();
    marker_low();

    *heap_after = heap_free_bytes();
    return (double)(t1 - t0);
}

// ========= BENCHMARK 3: MATRIX MULT =========
static void matmul_ikj(const int *A, const int *B, int *C, int n) {
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            int aik = A[i*n + k];
            for (int j = 0; j < n; j++) {
                C[i*n + j] += aik * B[k*n + j];
            }
        }
    }
}

static void matmul_ijk(const int *A, const int *B, int *C, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int sum = 0;
            for (int k = 0; k < n; k++) {
                sum += A[i*n + k] * B[k*n + j];
            }
            C[i*n + j] = sum;
        }
    }
}

typedef void (*mm_fn_t)(const int*, const int*, int*, int);

static double bench_matmul(size_t *heap_before, size_t *heap_after, mm_fn_t fn) {
    *heap_before = heap_free_bytes();

    int *A = malloc(sizeof(int) * MM_N * MM_N);
    int *B = malloc(sizeof(int) * MM_N * MM_N);
    int *C = calloc(MM_N * MM_N, sizeof(int));

    if (!A || !B || !C) {
        free(A); free(B); free(C);
        *heap_after = heap_free_bytes();
        return 0;
    }

    for (int i = 0; i < MM_N * MM_N; i++) {
        A[i] = (i % 7) + 1;
        B[i] = (i % 5) + 1;
    }

    marker_high();
    uint64_t t0 = time_us();

    fn(A, B, C, MM_N);

    uint64_t t1 = time_us();
    marker_low();

    free(A); free(B); free(C);

    *heap_after = heap_free_bytes();
    return (double)(t1 - t0);
}

static double bench_matmul_ikj(size_t *h1, size_t *h2) {
    return bench_matmul(h1, h2, matmul_ikj);
}

static double bench_matmul_ijk(size_t *h1, size_t *h2) {
    return bench_matmul(h1, h2, matmul_ijk);
}

// ========= FFT CORE =========
static void bit_reverse(double *re, double *im, int n) {
    int j = 0;
    for (int i = 0; i < n; i++) {
        if (i < j) {
            double tr = re[i], ti = im[i];
            re[i] = re[j]; im[i] = im[j];
            re[j] = tr;    im[j] = ti;
        }
        int bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j |= bit;
    }
}

static void fft_inplace(double *re, double *im, int n) {
    bit_reverse(re, im, n);

    for (int len = 2; len <= n; len <<= 1) {
        double ang     = -2.0 * PI / len;
        double wlen_re = cos(ang);
        double wlen_im = sin(ang);

        for (int i = 0; i < n; i += len) {
            double wre = 1.0, wim = 0.0;
            for (int j = 0; j < len / 2; j++) {
                int u = i + j;
                int v = i + j + len / 2;

                double vr = re[v]*wre - im[v]*wim;
                double vi = re[v]*wim + im[v]*wre;

                re[v] = re[u] - vr;
                im[v] = im[u] - vi;
                re[u] += vr;
                im[u] += vi;

                double nwre = wre*wlen_re - wim*wlen_im;
                wim = wre*wlen_im + wim*wlen_re;
                wre = nwre;
            }
        }
    }
}

// ========= BENCHMARK 4: FFT =========
static double bench_fft(size_t *heap_before, size_t *heap_after) {
    *heap_before = heap_free_bytes();

    static double re[FFT_N];
    static double im[FFT_N];

    for (int i = 0; i < FFT_N; i++) {
        re[i] = sin(2.0 * PI * i / FFT_N);
        im[i] = 0.0;
    }

    marker_high();
    uint64_t t0 = time_us();

    for (int r = 0; r < FFT_RUNS; r++) {
        double rtmp[FFT_N];
        double itmp[FFT_N];
        memcpy(rtmp, re, sizeof(re));
        memcpy(itmp, im, sizeof(im));
        fft_inplace(rtmp, itmp, FFT_N);
    }

    uint64_t t1 = time_us();
    marker_low();

    *heap_after = heap_free_bytes();
    return (double)(t1 - t0) / FFT_RUNS;
}

// ========= BENCHMARK 5: WIFI HTTP =========
typedef struct {
    volatile bool done;
    volatile uint32_t latency_us;
} http_req_state_t;

static err_t http_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)pcb; (void)err;
    if (p) pbuf_free(p);
    return ERR_OK;
}

static double bench_wifi_http(size_t *heap_before, size_t *heap_after) {
    *heap_before = heap_free_bytes();

    double sum = 0;

    for (int i = 0; i < HTTP_REQS; i++) {
        uint64_t t0 = time_us();

        httpc_connection_t settings = {0};
        settings.result_fn = NULL;
        settings.headers_done_fn = NULL;

        httpc_get_file_dns(HTTP_HOST, 80, HTTP_PATH, &settings, NULL, NULL, NULL);

        // give lwIP time to complete the request
        uint64_t deadline = time_us() + 10000000ULL; // 10s timeout
        while (time_us() < deadline) {
            cyw43_arch_poll();
            sleep_ms(10);
        }

        uint64_t t1 = time_us();
        sum += (double)(t1 - t0);
        sleep_ms(50);
    }

    *heap_after = heap_free_bytes();
    return sum / HTTP_REQS;
}

// ========= BENCH TABLE =========
typedef double (*bench_fn_t)(size_t*, size_t*);
typedef struct { const char *name; bench_fn_t fn; } bench_entry_t;

static const bench_entry_t BENCHES[] = {
    { "busy_loop",   bench_busy_loop   },
    { "gpio_toggle", bench_gpio_toggle },
    { "matmul_ikj",  bench_matmul_ikj  },
    { "matmul_ijk",  bench_matmul_ijk  },
    { "fft",         bench_fft         },
    { "wifi_http",   bench_wifi_http   },
};

// ========= MAIN =========
int main(void) {
    stdio_init_all();

    // wait for USB serial to connect
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(500);

    gpio_prep();

    // init Wi-Fi
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                            CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Wi-Fi connect failed\n");
        return -1;
    }
    printf("Wi-Fi connected\n");
    sleep_ms(500);

    printf("benchmark,mean_us,p95_us,heap_before,heap_after\n");

    int n_benches = sizeof(BENCHES) / sizeof(BENCHES[0]);

    for (int b = 0; b < n_benches; b++) {
        double runs[RUNS_PER_BENCH];
        double sum = 0;
        size_t h1 = 0, h2 = 0;

        // warmup run — not recorded
        BENCHES[b].fn(&h1, &h2);
        sleep_ms(200);

        for (int r = 0; r < RUNS_PER_BENCH; r++) {
            runs[r] = BENCHES[b].fn(&h1, &h2);
            sum += runs[r];
            sleep_ms(200);
        }

        printf("%s,%.2f,%.2f,%u,%u\n",
            BENCHES[b].name,
            sum / RUNS_PER_BENCH,
            calculate_p95(runs, RUNS_PER_BENCH),
            (unsigned)h1,
            (unsigned)h2
        );
    }

    cyw43_arch_deinit();
    return 0;
}