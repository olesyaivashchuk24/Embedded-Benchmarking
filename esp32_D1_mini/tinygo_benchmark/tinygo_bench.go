package main

import (
	"fmt"
	"machine"
	"math"
	"runtime"
	"sort"
	"time"
)

const (
	ITER_GPIO      = 1000
	MM_N           = 20
	RUNS_PER_BENCH = 5
	BUSY_ITER      = 5000

	FFT_N    = 256
	FFT_RUNS = 5
)

// ======== GLOBAL BUFFERS (no alloc during bench) ========

var (
	A = make([][]float64, MM_N)
	B = make([][]float64, MM_N)
	C = make([][]float64, MM_N)

	re   = make([]float64, FFT_N)
	im   = make([]float64, FFT_N)
	rtmp = make([]float64, FFT_N)
	itmp = make([]float64, FFT_N)
)

// ========= UTILS =========

func heapFree() uint64 {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	return m.Sys - m.Alloc
}

func p95(times []float64) float64 {
	sort.Float64s(times)
	idx := int(0.95 * float64(len(times)))
	if idx >= len(times) {
		idx = len(times) - 1
	}
	return times[idx]
}

// ========= BENCH 1 =========

func benchBusyLoop() (float64, uint64, uint64) {
	runtime.GC()
	h1 := heapFree()

	x := uint32(0)
	start := time.Now()

	for i := 0; i < BUSY_ITER; i++ {
		x = x*1664525 + 1013904223
	}

	elapsed := time.Since(start).Microseconds()
	h2 := heapFree()
	return float64(elapsed), h1, h2
}

// ========= BENCH 2 =========

func benchGpioToggle(pin machine.Pin) (float64, uint64, uint64) {
	runtime.GC()
	h1 := heapFree()

	level := false
	start := time.Now()

	for i := 0; i < ITER_GPIO; i++ {
		level = !level
		if level {
			pin.High()
		} else {
			pin.Low()
		}
	}

	elapsed := time.Since(start).Microseconds()
	h2 := heapFree()
	return float64(elapsed), h1, h2
}

// ========= MATRIX =========

func matmulIKJ() {
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

func matmulIJK() {
	for i := 0; i < MM_N; i++ {
		for j := 0; j < MM_N; j++ {
			C[i][j] = 0
		}
	}

	for i := 0; i < MM_N; i++ {
		for j := 0; j < MM_N; j++ {
			sum := 0.0
			for k := 0; k < MM_N; k++ {
				sum += A[i][k] * B[k][j]
			}
			C[i][j] = sum
		}
	}
}

func benchMatmul(fn func()) (float64, uint64, uint64) {
	runtime.GC()
	h1 := heapFree()

	start := time.Now()
	fn()
	elapsed := time.Since(start).Microseconds()

	h2 := heapFree()
	return float64(elapsed), h1, h2
}

// ========= FFT =========

func fftInplace(re, im []float64, n int) {
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

	start := time.Now()

	for i := 0; i < FFT_RUNS; i++ {
		copy(rtmp, re)
		copy(itmp, im)
		fftInplace(rtmp, itmp, FFT_N)
	}

	elapsed := time.Since(start).Microseconds()
	h2 := heapFree()
	return float64(elapsed) / FFT_RUNS, h1, h2
}

// ========= MAIN =========

func main() {

	time.Sleep(5 * time.Second)

	// init matrices
	for i := 0; i < MM_N; i++ {
		A[i] = make([]float64, MM_N)
		B[i] = make([]float64, MM_N)
		C[i] = make([]float64, MM_N)
		for j := 0; j < MM_N; j++ {
			A[i][j] = float64((j % 7) + 1)
			B[i][j] = float64((j % 5) + 1)
		}
	}

	// init FFT input
	for i := 0; i < FFT_N; i++ {
		re[i] = math.Sin(2 * math.Pi * float64(i) / FFT_N)
	}

	toggle := machine.GPIO2
	toggle.Configure(machine.PinConfig{Mode: machine.PinOutput})

	fmt.Println("benchmark,mean_us,p95_us,heap_before,heap_after")

	benches := []struct {
		name string
		fn   func() (float64, uint64, uint64)
	}{
		{"busy_loop", benchBusyLoop},
		{"gpio_toggle", func() (float64, uint64, uint64) { return benchGpioToggle(toggle) }},
		{"matmul_ikj", func() (float64, uint64, uint64) { return benchMatmul(matmulIKJ) }},
		{"matmul_ijk", func() (float64, uint64, uint64) { return benchMatmul(matmulIJK) }},
		{"fft", benchFFT},
	}

	for _, b := range benches {

		b.fn()
		time.Sleep(200 * time.Millisecond)

		var runs []float64
		var h1, h2 uint64

		for i := 0; i < RUNS_PER_BENCH; i++ {
			t, hb1, hb2 := b.fn()
			h1 = hb1
			h2 = hb2
			runs = append(runs, t)
			time.Sleep(200 * time.Millisecond)
		}

		var sum float64
		for _, v := range runs {
			sum += v
		}

		fmt.Printf("%s,%.2f,%.2f,%d,%d\n",
			b.name,
			sum/float64(len(runs)),
			p95(runs),
			h1,
			h2,
		)
	}
}
