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
var marker = machine.GPIO3

// ========= CONFIG =========
const (
	ITER_GPIO      = 1000
	BUSY_ITER      = 5000
	MM_N           = 32 // 32x32 matrix (matches paper)
	RUNS_PER_BENCH = 20 // 20 runs for meaningful p95
	FFT_N          = 256
	FFT_RUNS       = 5
)

// ========= GLOBAL BUFFERS (no alloc during bench) =========
var (
	re   = make([]float64, FFT_N)
	im   = make([]float64, FFT_N)
	rtmp = make([]float64, FFT_N)
	itmp = make([]float64, FFT_N)
)

// ========= MARKER =========
func markerHigh() { marker.High() }
func markerLow()  { marker.Low() }

// ========= HEAP =========
func heapFree() uint64 {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	return m.Sys - m.Alloc
}

// ========= UTILS =========
func p95(arr []float64) float64 {
	cp := make([]float64, len(arr))
	copy(cp, arr)
	sort.Float64s(cp)
	idx := int(0.95 * float64(len(cp)))
	if idx >= len(cp) {
		idx = len(cp) - 1
	}
	return cp[idx]
}

func mean(arr []float64) float64 {
	var sum float64
	for _, v := range arr {
		sum += v
	}
	return sum / float64(len(arr))
}

func meanU64(arr []uint64) uint64 {
	var sum uint64
	for _, v := range arr {
		sum += v
	}
	return sum / uint64(len(arr))
}

// ========= GPIO INIT =========
func gpioPrep() {
	toggle.Configure(machine.PinConfig{Mode: machine.PinOutput})
	marker.Configure(machine.PinConfig{Mode: machine.PinOutput})
}

// ========= BENCH 1: BUSY LOOP =========
func benchBusyLoop() (float64, uint64, uint64) {
	runtime.GC()
	h1 := heapFree()
	var x uint32

	markerHigh()
	t0 := time.Now()
	for i := 0; i < BUSY_ITER; i++ {
		x = x*1664525 + 1013904223
	}
	elapsed := time.Since(t0).Microseconds()
	markerLow()
	_ = x // prevent compiler from optimizing the loop away

	h2 := heapFree()
	return float64(elapsed), h1, h2
}

// ========= BENCH 2: GPIO TOGGLE =========
func benchGpioToggle() (float64, uint64, uint64) {
	runtime.GC()
	h1 := heapFree()
	level := false

	markerHigh()
	t0 := time.Now()
	for i := 0; i < ITER_GPIO; i++ {
		level = !level
		if level {
			toggle.High()
		} else {
			toggle.Low()
		}
	}
	elapsed := time.Since(t0).Microseconds()
	markerLow()

	h2 := heapFree()
	return float64(elapsed), h1, h2
}

// ========= BENCH 3: MATRIX =========
func matmulIKJ(A, B, C [][]int) {
	for i := 0; i < MM_N; i++ {
		for j := 0; j < MM_N; j++ {
			C[i][j] = 0
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

func matmulIJK(A, B, C [][]int) {
	for i := 0; i < MM_N; i++ {
		for j := 0; j < MM_N; j++ {
			C[i][j] = 0
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

func benchMatmul(fn func([][]int, [][]int, [][]int)) (float64, uint64, uint64) {
	runtime.GC()
	h1 := heapFree()

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

	markerHigh()
	t0 := time.Now()
	fn(A, B, C)
	elapsed := time.Since(t0).Microseconds()
	markerLow()

	h2 := heapFree()
	return float64(elapsed), h1, h2
}

// ========= BENCH 4: FFT =========
func bitReverse(re, im []float64, n int) {
	j := 0
	for i := 0; i < n; i++ {
		if i < j {
			re[i], re[j] = re[j], re[i]
			im[i], im[j] = im[j], im[i]
		}
		bit := n >> 1
		for j&bit != 0 {
			j ^= bit
			bit >>= 1
		}
		j |= bit
	}
}

func fftInplace(re, im []float64, n int) {
	bitReverse(re, im, n)

	length := 2
	for length <= n {
		ang := -2 * math.Pi / float64(length)
		wlenRe := math.Cos(ang)
		wlenIm := math.Sin(ang)

		for i := 0; i < n; i += length {
			wre := 1.0
			wim := 0.0
			for j := 0; j < length/2; j++ {
				u := i + j
				v := i + j + length/2

				vr := re[v]*wre - im[v]*wim
				vi := re[v]*wim + im[v]*wre

				re[v] = re[u] - vr
				im[v] = im[u] - vi
				re[u] += vr
				im[u] += vi

				nwre := wre*wlenRe - wim*wlenIm
				wim = wre*wlenIm + wim*wlenRe
				wre = nwre
			}
		}
		length <<= 1
	}
}

func benchFFT() (float64, uint64, uint64) {
	runtime.GC()
	h1 := heapFree()

	markerHigh()
	t0 := time.Now()
	for r := 0; r < FFT_RUNS; r++ {
		copy(rtmp, re)
		copy(itmp, im)
		fftInplace(rtmp, itmp, FFT_N) // was missing in original!
	}
	elapsed := time.Since(t0).Microseconds()
	markerLow()

	h2 := heapFree()
	return float64(elapsed) / FFT_RUNS, h1, h2
}

// ========= RUNNER =========
func runBench(name string, fn func() (float64, uint64, uint64)) {
	// warmup — not recorded
	fn()
	time.Sleep(200 * time.Millisecond)

	var runs []float64
	var h1s, h2s []uint64

	for i := 0; i < RUNS_PER_BENCH; i++ {
		t, h1, h2 := fn()
		runs = append(runs, t)
		h1s = append(h1s, h1)
		h2s = append(h2s, h2)
		time.Sleep(200 * time.Millisecond)
	}

	fmt.Printf("%s,%.2f,%.2f,%d,%d\n",
		name,
		mean(runs),
		p95(runs),
		meanU64(h1s),
		meanU64(h2s),
	)
}

// ========= MAIN =========
func main() {
	gpioPrep()

	// init FFT input once, reuse global buffers
	for i := 0; i < FFT_N; i++ {
		re[i] = math.Sin(2 * math.Pi * float64(i) / FFT_N)
	}

	time.Sleep(2 * time.Second)

	fmt.Println("benchmark,mean_us,p95_us,heap_before,heap_after")

	runBench("busy_loop", benchBusyLoop)
	runBench("gpio_toggle", benchGpioToggle)
	runBench("matmul_ikj", func() (float64, uint64, uint64) { return benchMatmul(matmulIKJ) })
	runBench("matmul_ijk", func() (float64, uint64, uint64) { return benchMatmul(matmulIJK) })
	runBench("fft", benchFFT)

	for {
		time.Sleep(time.Second)
	}
}
