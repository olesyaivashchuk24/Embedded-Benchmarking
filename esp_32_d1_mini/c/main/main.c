#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

// ========= PIN ASSIGNMENTS =========
#define PIN_TOGGLE     GPIO_NUM_2
#define PIN_MARKER     GPIO_NUM_4

// ========= BENCHMARK CONFIG =========
#define ITER_GPIO      1000
#define MM_N           20
#define RUNS_PER_BENCH 5
#define BUSY_ITER      5000

// ========= FFT CONFIG =========
#define FFT_N    256
#define FFT_RUNS 10
#define PI 3.14159265358979323846

// ========= WIFI CONFIG =========
#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASS "WIFI_PASS"
#define HTTP_URL "http://httpbin.org/get"
#define HTTP_REQS 20

// ========= UTILS =========
static inline void marker_high(void){ gpio_set_level(PIN_MARKER, 1); }
static inline void marker_low(void) { gpio_set_level(PIN_MARKER, 0); }

static int compare_doubles(const void *a, const void *b) {
    double da = *(double*)a;
    double db = *(double*)b;
    return (da > db) - (da < db);
}

static double calculate_p95(double *times, int n_runs) {
    qsort(times, n_runs, sizeof(double), compare_doubles);
    int idx = (int)(0.95 * n_runs);
    if (idx >= n_runs) idx = n_runs - 1;
    return times[idx];
}

static void gpio_prep(void) {
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_TOGGLE) | (1ULL << PIN_MARKER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    gpio_set_level(PIN_TOGGLE, 0);
    gpio_set_level(PIN_MARKER, 0);
}

static size_t heap_free_bytes(void) {
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

// ========= BENCHMARK 1: GPIO TOGGLE =========
static double bench_gpio_toggle(size_t *heap_before, size_t *heap_after) {
    *heap_before = heap_free_bytes();

    marker_high();
    int level = 0;
    int64_t t0 = esp_timer_get_time();

    for (int i = 0; i < ITER_GPIO; i++) {
        level ^= 1;
        gpio_set_level(PIN_TOGGLE, level);

        if ((i % 1000) == 0) {
            esp_task_wdt_reset();
            taskYIELD();
        }
    }

    int64_t t1 = esp_timer_get_time();
    marker_low();

    *heap_after = heap_free_bytes();
    return (double)(t1 - t0);
}

// ========= BENCHMARK 2: MATRIX MULT =========
static void matmul_ikj(const int *A, const int *B, int *C, int n) {
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            int aik = A[i*n + k];
            for (int j = 0; j < n; j++) {
                C[i*n + j] += aik * B[k*n + j];

                if ((j % 100) == 0) {
                    esp_task_wdt_reset();
                    taskYIELD();
                }
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

                if ((k % 100) == 0) {
                    esp_task_wdt_reset();
                    taskYIELD();
                }
            }
            C[i*n + j] = sum;
        }
    }
}

typedef void (*mm_fn_t)(const int*, const int*, int*, int);

static double bench_matmul(size_t *heap_before, size_t *heap_after, mm_fn_t fn) {
    *heap_before = heap_free_bytes();

    int *A = heap_caps_malloc(sizeof(int)*MM_N*MM_N, MALLOC_CAP_8BIT);
    int *B = heap_caps_malloc(sizeof(int)*MM_N*MM_N, MALLOC_CAP_8BIT);
    int *C = heap_caps_calloc(MM_N*MM_N, sizeof(int), MALLOC_CAP_8BIT);

    if (!A || !B || !C) {
        if (A) free(A);
        if (B) free(B);
        if (C) free(C);
        *heap_after = heap_free_bytes();
        return 0;
    }

    for (int i = 0; i < MM_N*MM_N; i++) {
        A[i] = (i % 7) + 1;
        B[i] = (i % 5) + 1;
    }

    marker_high()    int64_t t0 = esp_timer_get_time();

    fn(A, B, C, MM_N);

    int64_t t1 = esp_timer_get_time();
    marker_low();

    free(A);
    free(B);
    free(C);

    
    *heap_after = heap_free_bytes();

    return (double)(t1 - t0);
}

static double bench_matmul_ikj(size_t *h1, size_t *h2) {
    return bench_matmul(h1, h2, matmul_ikj);
}

static double bench_matmul_ijk(size_t *h1, size_t *h2) {
    return bench_matmul(h1, h2, matmul_ijk);
}

// ========= BENCHMARK 3: BUSY LOOP =========
static double bench_busy_loop(size_t *heap_before, size_t *heap_after) {
    *heap_before = heap_free_bytes();

    volatile uint32_t x = 0;
    marker_high();
    int64_t t0 = esp_timer_get_time();

    for (uint32_t i = 0; i < BUSY_ITER; i++) {
        x = x * 1664525u + 1013904223u;

        if ((i % 100000) == 0) {
            esp_task_wdt_reset();
            taskYIELD();
        }
    }

    int64_t t1 = esp_timer_get_time();
    marker_low();

    *heap_after = heap_free_bytes();
    return (double)(t1 - t0);
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
        double ang = -2.0 * PI / len;
        double wlen_re = cos(ang);
        double wlen_im = sin(ang);

        for (int i = 0; i < n; i += len) {
            double wre = 1.0, wim = 0.0;
            for (int j = 0; j < len/2; j++) {
                int u = i + j;
                int v = i + j + len/2;

                double vr = re[v]*wre - im[v]*wim;
                double vi = re[v]*wim + im[v]*wre;

                re[v] = re[u] - vr;
                im[v] = im[u] - vi;
                re[u] += vr;
                im[u] += vi;

                double nwre = wre*wlen_re - wim*wlen_im;
                wim = wre*wlen_im + wim*wlen_re;
                wre = nwre;

                if ((j % 32) == 0) {
                    esp_task_wdt_reset();
                    taskYIELD();
                }
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
    int64_t t0 = esp_timer_get_time();

    for (int r = 0; r < FFT_RUNS; r++) {
        double rtmp[FFT_N];
        double itmp[FFT_N];
        memcpy(rtmp, re, sizeof(re));
        memcpy(itmp, im, sizeof(im));
        fft_inplace(rtmp, itmp, FFT_N);
    }

    int64_t t1 = esp_timer_get_time();
    marker_low();

    *heap_after = heap_free_bytes();
    return (double)(t1 - t0) / FFT_RUNS;
}

// ========= WIFI HTTP BENCHMARK =========
static int http_get_latency_us(void) {
    esp_http_client_config_t config = {
        .url = HTTP_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    int64_t t0 = esp_timer_get_time();
    esp_http_client_perform(client);
    int64_t t1 = esp_timer_get_time();

    esp_http_client_cleanup(client);
    return (int)(t1 - t0);
}

static double bench_wifi_http(size_t *heap_before, size_t *heap_after) {
    *heap_before = heap_free_bytes();

    double sum = 0;

    esp_task_wdt_delete(NULL);

    for (int i = 0; i < HTTP_REQS; i++) {
        sum += http_get_latency_us();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_task_wdt_add(NULL);

    *heap_after = heap_free_bytes();
    return (sum / HTTP_REQS);
}

// ========= BENCH TABLE =========
typedef double (*bench_fn_t)(size_t*, size_t*);
typedef struct { const char *name; bench_fn_t fn; } bench_entry_t;

static const bench_entry_t BENCHES[] = {
    { "busy_loop",   bench_busy_loop },
    { "gpio_toggle", bench_gpio_toggle },
    { "matmul_ikj",  bench_matmul_ikj },
    { "matmul_ijk",  bench_matmul_ijk },
    { "fft",         bench_fft },
    { "wifi_http",   bench_wifi_http },
};

// ========= WIFI EVENT =========
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ========= WIFI INIT =========
static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// ========= TASK =========
static void bench_task(void *arg) {
    esp_task_wdt_add(NULL);

    gpio_prep();
    vTaskDelay(pdMS_TO_TICKS(500));

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    printf("benchmark,   mean_us,  p95_us, heap_before, heap_after\n\n");

    for (int b = 0; b < sizeof(BENCHES)/sizeof(BENCHES[0]); b++) {
        double runs[RUNS_PER_BENCH];
        double sum = 0;
        size_t h1, h2;

        for (int r = 0; r < RUNS_PER_BENCH; r++) {
            runs[r] = BENCHES[b].fn(&h1, &h2);
            sum += runs[r];
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        printf("%s,   %.2f,  %.2f, %u, %u\n\n",
            BENCHES[b].name,
            sum / RUNS_PER_BENCH,
            calculate_p95(runs, RUNS_PER_BENCH),
            (unsigned)h1,
            (unsigned)h2
        );
    }

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

// ========= APP MAIN =========
void app_main(void) {
    nvs_flash_init();
    wifi_init();

    xTaskCreatePinnedToCore(
        bench_task,
        "bench",
        8192,
        NULL,
        5,
        NULL,
        0
    );
}
