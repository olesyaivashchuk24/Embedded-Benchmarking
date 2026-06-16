# Embedded Benchmarking

> Comparing C, MicroPython, and TinyGo on ESP32 D1 Mini and Raspberry Pi Pico 2W.

A reproducible benchmark suite measuring the performance impact of programming language and hardware platform choices in embedded systems. Each benchmark is implemented identically across all languages, enabling fair cross-platform and cross-language comparisons.

**Author:** Olesya Ivashchuk · Walsh University  
**Advisor:** David Good

---

## Platforms

| Platform | Architecture | Highlights |
|---|---|---|
| ESP32 D1 Mini | Xtensa LX7 dual-core | Built-in Wi-Fi, larger library ecosystem |
| Raspberry Pi Pico 2W | RP2040 (ARM Cortex-M0+) | Energy-efficient, deterministic peripheral control |

## Languages

| Language | Type | Notes |
|---|---|---|
| C (ESP-IDF / Pico SDK) | Compiled | Highest control, minimal overhead |
| MicroPython | Interpreted | Fastest to develop, highest runtime overhead |
| TinyGo | Compiled (Go) | Middle ground — modern syntax, competitive performance |

---

## Benchmarks

Each platform runs the same test suite:

- **busy_loop** — tight loop iteration, tests raw execution overhead
- **matmul_ikj** — 32×32 matrix multiplication, memory-bound computation
- **fft** — Fast Fourier Transform, floating-point and library performance
- **gpio_toggle** — toggle a GPIO pin repeatedly, measures hardware abstraction cost
- **memory** — heap usage before and after benchmark execution
- **wifi_http** — HTTP GET over Wi-Fi (ESP32 and Pico 2W only; TinyGo excluded due to limited Wi-Fi support)

Each test reports average execution time, p95, and memory delta.

---

## Results summary

### Computation (µs, lower is better)

| Benchmark | Platform | C | MicroPython | TinyGo |
|---|---|---|---|---|
| matmul_ikj | ESP32 | **5,200** | — | 9,400 |
| matmul_ikj | Pico 2W | **4,728** | 67,000 | 5,210 |
| fft | ESP32 | 10,900 | — | **8,880** |
| fft | Pico 2W | 8,421 | — | **7,312** |
| busy_loop | Pico 2W | 1,098 | ~120,000+ | **843** |

### GPIO toggle (µs, lower is better)

| Platform | C | MicroPython | TinyGo |
|---|---|---|---|
| ESP32 | 526 | — | **95** |
| Pico 2W | 119 | 6,124 | **74** |

### Memory usage (KB, lower is better)

| Platform | C | MicroPython | TinyGo |
|---|---|---|---|
| ESP32 | **221** | — | 288 |
| Pico 2W | **264** | 447 | 509 |

### Wi-Fi HTTP latency (average, TinyGo excluded)

| Platform | C | MicroPython |
|---|---|---|
| ESP32 | ~3,900 ms | ~4,100 ms |
| Pico 2W | ~412 ms | ~352 ms |

> Wi-Fi latency reflects the maturity of each platform's networking stack. ESP32 values include full round-trip time; Pico values measure a less optimized Wi-Fi stack.

---

## Key findings

**C** delivers the most predictable performance and lowest memory usage across all benchmarks. Its minimal abstraction and absence of a runtime environment make it the best choice when maximum efficiency and deterministic behavior are required.

**TinyGo** is competitive with C in computational tasks and outperforms it in FFT and GPIO on both platforms. It uses more memory due to runtime overhead, and Wi-Fi support is limited. A strong option when development ergonomics matter alongside performance.

**MicroPython** is consistently the slowest due to its interpreted execution model — often 10–100× slower than compiled languages. It remains well-suited for rapid prototyping and education where development speed outweighs execution speed.

**Platform differences** matter independently of language: the ESP32 has a more mature Wi-Fi ecosystem and performs better in networking tasks, while the Pico 2W shows strong results in computational and peripheral operations due to its lighter architecture.

---

## Repository structure

```
Embedded-Benchmarking/
├── esp_32_d1_mini/
│   ├── c/           # ESP-IDF implementation
│   ├── micropython/ # MicroPython implementation
│   └── tinygo/      # TinyGo implementation
├── pico_pi_2w/
│   ├── c/           # Pico SDK implementation
│   ├── micropython/ # MicroPython implementation
│   └── tinygo/      # TinyGo implementation
└── README.md
```

---

## Running the benchmarks

### ESP32 — C (ESP-IDF)

```bash
cd esp_32_d1_mini/c
idf.py build flash monitor
```

### ESP32 — MicroPython

Upload files to the board using `mpremote` or Thonny, then run `main.py`.

### ESP32 — TinyGo

```bash
cd esp_32_d1_mini/tinygo
tinygo flash -target=esp32-d1mini .
```

### Pico 2W — C (Pico SDK)

```bash
cd pico_pi_2w/c
mkdir build && cd build
cmake .. && make
# Copy the .uf2 file to the Pico in BOOTSEL mode
```

### Pico 2W — MicroPython

Upload files via `mpremote` or Thonny and run `main.py`.

### Pico 2W — TinyGo

```bash
cd pico_pi_2w/tinygo
tinygo flash -target=pico2w .
```

---

## Methodology notes

- Each test was repeated multiple times; results report the average and p95.
- A short delay was inserted between runs to reduce thermal and scheduling artifacts.
- Compiler optimizations use the default configuration for each SDK — reflecting real-world development conditions.
- Implementations are functionally equivalent but not byte-for-byte identical across languages due to SDK and runtime differences.
- The busy_loop result for TinyGo on ESP32 (~1 µs) is an optimization artifact and is excluded from comparisons.

---

## References

1. ESP Boards, "ESP32 D1 Mini Technical Documentation," espboards.dev, 2024.
2. A. M. Ali et al., "Performance Evaluation of Embedded Systems Using ESP32 Microcontroller," *Electronics*, vol. 12, no. 1, p. 143, 2023.
3. M. M. Ahmed and S. K. Gupta, "Comparative Analysis of Programming Models for IoT and Embedded Systems," Springer, 2020.
4. Espressif Systems, "ESP32 Technical Reference Manual," 2024.