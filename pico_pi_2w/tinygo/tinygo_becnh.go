package main

import (
	"fmt"
	"machine"
	"math"
	"runtime"
	"sort"
	"time"
)

// ========= PINS =========
var toggle = machine.GPIO2
var marker = machine.GPIO4

// ========= CONFIG =========
const (
	ITER_GPIO      = 1000
	BUSY_ITER      = 5000
	MM_N           = 20
	RUNS_PER_BENCH = 5
	FFT_N          = 256
	FFT_RUNS       = 5
)

// ========= MARKER =========
func markerHigh() { marker.High() }
func markerLow()  { marker.Low() }

// ========= TIMER =========
func nowUS() int64 {
	return time.Now().UnixNano() / 1000
}

// ========= HEAP (approx) =========
func heapFree() uint64 {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	return m.Sys - m.Alloc
}

// ========= P95 =========
func p95(arr []float64) float64 {
	sort.Float64s(arr)
	idx := int(0.95 * float64(len(arr)))
	if idx >= len(arr) {
		idx = len(arr) - 1
	}
	return arr[idx]
}

// ========= GPIO INIT =========
func gpioPrep() {
	toggle.Configure(machine.PinConfig{Mode: machine.PinOutput})
	marker.Configure(machine.PinConfig{Mode: machine.PinOutput})
}

// ========= BUSY =========
func benchBusyLoop() (float64, uint64, uint64) {
	h1 := heapFree()

	var x uint32
	markerHigh()
	t0 := nowUS()

	for i := 0; i < BUSY_ITER; i++ {
		x = x*1664525 + 1013904223
	}

	t1 := nowUS()
	markerLow()

	h2 := heapFree()
	return float64(t1 - t0), h1, h2
}

// ========= GPIO =========
func benchGpioToggle() (float64, uint64, uint64) {
	h1 := heapFree()

	level := false
	markerHigh()
	t0 := nowUS()

	for i := 0; i < ITER_GPIO; i++ {
		level = !level
		if level {
			toggle.High()
		} else {
			toggle.Low()
		}
	}

	t1 := nowUS()
	markerLow()

	h2 := heapFree()
	return float64(t1 - t0), h1, h2
}

// ========= MATMUL =========
func matmulIKJ() {
	A := make([][]int, MM_N)
	B := make([][]int, MM_N)
	C := make([][]int, MM_N)

	for i := 0; i < MM_N; i++ {
		A[i] = make([]int, MM_N)
		B[i] = make([]int, MM_N)
		C[i] = make([]int, MM_N)
		for j := 0; j < MM_N; j++ {
			A[i][j] = (j % 7) + 1
			B[i][j] = (j % 5) + 1
		}
	}

	for i := 0; i < MM_N; i++ {
		for k := 0; k < MM_N; k++ {
			aik := A[i][k]
			for j := 0; j < MM_N; j++ {
				C[i][j] += aik * B[k][j]
			}
		}
	}
}

func matmulIJK() {
	A := make([][]int, MM_N)
	B := make([][]int, MM_N)
	C := make([][]int, MM_N)

	for i := 0; i < MM_N; i++ {
		A[i] = make([]int, MM_N)
		B[i] = make([]int, MM_N)
		C[i] = make([]int, MM_N)
		for j := 0; j < MM_N; j++ {
			A[i][j] = (j % 7) + 1
			B[i][j] = (j % 5) + 1
		}
	}

	for i := 0; i < MM_N; i++ {
		for j := 0; j < MM_N; j++ {
			sum := 0
			for k := 0; k < MM_N; k++ {
				sum += A[i][k] * B[k][j]
			}
			C[i][j] = sum
		}
	}
}

func bench(fn func()) (float64, uint64, uint64) {
	h1 := heapFree()

	markerHigh()
	t0 := nowUS()

	fn()

	t1 := nowUS()
	markerLow()

	h2 := heapFree()
	return float64(t1 - t0), h1, h2
}

// ========= FFT =========
func benchFFT() (float64, uint64, uint64) {
	h1 := heapFree()

	re := make([]float64, FFT_N)
	im := make([]float64, FFT_N)

	for i := 0; i < FFT_N; i++ {
		re[i] = math.Sin(2 * math.Pi * float64(i) / FFT_N)
	}

	markerHigh()
	t0 := nowUS()

	for r := 0; r < FFT_RUNS; r++ {
		rtmp := make([]float64, FFT_N)
		itmp := make([]float64, FFT_N)
		copy(rtmp, re)
		copy(itmp, im)
	}

	t1 := nowUS()
	markerLow()

	h2 := heapFree()
	return float64(t1-t0) / FFT_RUNS, h1, h2
}

// ========= RUNNER =========
func runBench(name string, fn func() (float64, uint64, uint64)) {

	var runs []float64
	var h1s []uint64
	var h2s []uint64

	for i := 0; i < RUNS_PER_BENCH; i++ {
		t, h1, h2 := fn()
		runs = append(runs, t)
		h1s = append(h1s, h1)
		h2s = append(h2s, h2)
		time.Sleep(200 * time.Millisecond)
	}

	var sum float64
	var sumH1, sumH2 uint64

	for i := range runs {
		sum += runs[i]
		sumH1 += h1s[i]
		sumH2 += h2s[i]
	}

	fmt.Printf("%s, %.2f, %.2f, %d, %d\n",
		name,
		sum/float64(len(runs)),
		p95(runs),
		sumH1/uint64(len(h1s)),
		sumH2/uint64(len(h2s)),
	)
}

// ========= MAIN =========
func main() {

	gpioPrep()
	time.Sleep(2 * time.Second)

	fmt.Println("benchmark, mean_us, p95_us, heap_before, heap_after")

	runBench("busy_loop", benchBusyLoop)
	runBench("gpio_toggle", benchGpioToggle)
	runBench("matmul_ikj", func() (float64, uint64, uint64) {
		return bench(matmulIKJ)
	})
	runBench("matmul_ijk", func() (float64, uint64, uint64) {
		return bench(matmulIJK)
	})
	runBench("fft", benchFFT)

	for {
		time.Sleep(time.Second)
	}
}
