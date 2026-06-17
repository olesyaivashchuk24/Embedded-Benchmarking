import time
import gc
import sys
import math
import network
import urequests
from machine import Pin

# ========= ENV INFO =========
print("micropython:", sys.implementation)

# ========= WIFI CONFIG =========
WIFI_SSID     = "YOUR_SSID"
WIFI_PASSWORD = "YOUR_PASSWORD"

# ========= GC CONTROL =========
gc.collect()
gc.threshold(gc.mem_free() // 4)

# ========= PIN ASSIGNMENTS =========
PIN_TOGGLE = 3
PIN_MARKER = 10

toggle = Pin(PIN_TOGGLE, Pin.OUT)
marker = Pin(PIN_MARKER, Pin.OUT)

def marker_high():
    marker.value(1)

def marker_low():
    marker.value(0)

# ========= BENCH CONFIG =========
ITER_GPIO      = 1000
MM_N           = 32      # 32x32 matrix (matches paper)
RUNS_PER_BENCH = 20      # 20 runs for meaningful p95
BUSY_ITER      = 5000
FFT_N          = 256
FFT_RUNS       = 5
HTTP_URL       = "http://httpbin.org/get"
HTTP_REQS      = 10

# ========= UTILS =========
def heap_free_bytes():
    gc.collect()
    return gc.mem_free()

def p95(times):
    sorted_times = sorted(times)
    idx = min(int(0.95 * len(sorted_times)), len(sorted_times) - 1)
    return sorted_times[idx]

# ========= WIFI =========
def wifi_connect(ssid, password):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        wlan.connect(ssid, password)
        while not wlan.isconnected():
            time.sleep_ms(100)
    print("wifi connected:", wlan.ifconfig()[0])
    return wlan

# ========= BENCH 1: BUSY LOOP =========
def bench_busy_loop():
    h1 = heap_free_bytes()
    x = 0

    marker_high()
    t0 = time.ticks_us()

    for _ in range(BUSY_ITER):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFF

    t1 = time.ticks_us()
    marker_low()

    h2 = heap_free_bytes()
    return time.ticks_diff(t1, t0), h1, h2

# ========= BENCH 2: GPIO =========
def bench_gpio_toggle():
    h1 = heap_free_bytes()
    level = 0

    marker_high()
    t0 = time.ticks_us()

    for _ in range(ITER_GPIO):
        level ^= 1
        toggle.value(level)

    t1 = time.ticks_us()
    marker_low()

    h2 = heap_free_bytes()
    return time.ticks_diff(t1, t0), h1, h2

# ========= BENCH 3: MATRIX =========
def matmul_ikj():
    A = [[(i % 7) + 1 for i in range(MM_N)] for _ in range(MM_N)]
    B = [[(i % 5) + 1 for i in range(MM_N)] for _ in range(MM_N)]
    C = [[0] * MM_N for _ in range(MM_N)]

    for i in range(MM_N):
        for k in range(MM_N):
            aik = A[i][k]
            for j in range(MM_N):
                C[i][j] += aik * B[k][j]

def matmul_ijk():
    A = [[(i % 7) + 1 for i in range(MM_N)] for _ in range(MM_N)]
    B = [[(i % 5) + 1 for i in range(MM_N)] for _ in range(MM_N)]
    C = [[0] * MM_N for _ in range(MM_N)]

    for i in range(MM_N):
        for j in range(MM_N):
            s = 0
            for k in range(MM_N):
                s += A[i][k] * B[k][j]
            C[i][j] = s

def bench_matmul(fn):
    h1 = heap_free_bytes()

    marker_high()
    t0 = time.ticks_us()
    fn()
    t1 = time.ticks_us()
    marker_low()

    h2 = heap_free_bytes()
    return time.ticks_diff(t1, t0), h1, h2

# ========= BENCH 4: FFT =========
def bit_reverse(re, im, n):
    j = 0
    for i in range(n):
        if i < j:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit

def fft_inplace(re, im, n):
    bit_reverse(re, im, n)

    length = 2
    while length <= n:
        ang = -2 * math.pi / length
        wlen_re = math.cos(ang)
        wlen_im = math.sin(ang)

        for i in range(0, n, length):
            wre = 1.0
            wim = 0.0
            for j in range(length // 2):
                u = i + j
                v = i + j + length // 2

                vr = re[v] * wre - im[v] * wim
                vi = re[v] * wim + im[v] * wre

                re[v] = re[u] - vr
                im[v] = im[u] - vi
                re[u] += vr
                im[u] += vi

                nwre = wre * wlen_re - wim * wlen_im
                wim  = wre * wlen_im + wim * wlen_re
                wre  = nwre

        length <<= 1

def bench_fft():
    h1 = heap_free_bytes()

    re = [math.sin(2 * math.pi * i / FFT_N) for i in range(FFT_N)]
    im = [0.0] * FFT_N

    marker_high()
    t0 = time.ticks_us()

    for _ in range(FFT_RUNS):
        rtmp = re[:]
        itmp = im[:]
        fft_inplace(rtmp, itmp, FFT_N)

    t1 = time.ticks_us()
    marker_low()

    h2 = heap_free_bytes()
    return time.ticks_diff(t1, t0) / FFT_RUNS, h1, h2

# ========= BENCH 5: WIFI HTTP =========
def bench_wifi_http():
    h1 = heap_free_bytes()
    total = 0

    marker_high()

    for _ in range(HTTP_REQS):
        start = time.ticks_us()
        r = urequests.get(HTTP_URL)
        r.close()
        end = time.ticks_us()
        total += time.ticks_diff(end, start)
        time.sleep_ms(50)

    marker_low()

    h2 = heap_free_bytes()
    return total / HTTP_REQS, h1, h2

# ========= BENCH TABLE =========
BENCHES = [
    ("busy_loop",   bench_busy_loop),
    ("gpio_toggle", bench_gpio_toggle),
    ("matmul_ikj",  lambda: bench_matmul(matmul_ikj)),
    ("matmul_ijk",  lambda: bench_matmul(matmul_ijk)),
    ("fft",         bench_fft),
    ("wifi_http",   bench_wifi_http),
]

# ========= RUN =========
time.sleep_ms(500)

# connect before warmup so wifi_http bench works correctly
wifi_connect(WIFI_SSID, WIFI_PASSWORD)

print("benchmark,mean_us,p95_us,heap_before,heap_after")

for name, fn in BENCHES:
    # warmup run — not recorded
    fn()
    time.sleep_ms(100)

    runs = []
    h1 = h2 = 0

    for _ in range(RUNS_PER_BENCH):
        t, h1, h2 = fn()
        runs.append(t)
        time.sleep_ms(200)

    print("{},{:.2f},{:.2f},{},{}".format(
        name,
        sum(runs) / len(runs),
        p95(runs),
        h1,
        h2
    ))