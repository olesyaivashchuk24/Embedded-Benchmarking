package main

import (
	"fmt"
	"machine"
	"math"
	"time"
)

// ========= PIN =========
var toggle = machine.GPIO2
var marker = machine.GPIO4

// ========= CONFIG =========
const ITER_GPIO = 1000
const BUSY_ITER = 5000

const MM_N = 20
const RUNS_PER_BENCH = 5

const FFT_N = 256
const FFT_RUNS = 10

// ========= MARKER =========
func markerHigh() {
	marker.High()
}

func markerLow() {
	marker.Low()
}

// ========= TIMER =========
func nowUS() int64 {
	return time.Now().UnixNano() / 1000
}

// ========= GPIO INIT =========
func gpioPrep() {

	toggle.Configure(machine.PinConfig{Mode: machine.PinOutput})
	marker.Configure(machine.PinConfig{Mode: machine.PinOutput})

	toggle.Low()
	marker.Low()
}

// ========= BUSY LOOP =========
func benchBusyLoop() float64 {

	var x uint32

	markerHigh()
	t0 := nowUS()

	for i := 0; i < BUSY_ITER; i++ {
		x = x*1664525 + 1013904223
	}

	t1 := nowUS()
	markerLow()

	return float64(t1 - t0)
}

// ========= GPIO BENCH =========
func benchGpioToggle() float64 {

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

	return float64(t1 - t0)
}

// ========= MATRIX =========
func matmul(A []int, B []int, C []int, n int) {

	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {

			sum := 0

			for k := 0; k < n; k++ {
				sum += A[i*n+k] * B[k*n+j]
			}

			C[i*n+j] = sum
		}
	}
}

func benchMatmul() float64 {

	A := make([]int, MM_N*MM_N)
	B := make([]int, MM_N*MM_N)
	C := make([]int, MM_N*MM_N)

	for i := range A {
		A[i] = (i % 7) + 1
		B[i] = (i % 5) + 1
	}

	markerHigh()
	t0 := nowUS()

	matmul(A, B, C, MM_N)

	t1 := nowUS()
	markerLow()

	return float64(t1 - t0)
}

// ========= FFT =========
func bitReverse(re []float64, im []float64, n int) {

	j := 0

	for i := 0; i < n; i++ {

		if i < j {

			re[i], re[j] = re[j], re[i]
			im[i], im[j] = im[j], im[i]
		}

		bit := n >> 1

		for (j & bit) != 0 {
			j ^= bit
			bit >>= 1
		}

		j |= bit
	}
}

func fft(re []float64, im []float64, n int) {

	bitReverse(re, im, n)

	for length := 2; length <= n; length <<= 1 {

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
	}
}

func benchFFT() float64 {

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

		fft(rtmp, itmp, FFT_N)
	}

	t1 := nowUS()
	markerLow()

	return float64(t1-t0) / FFT_RUNS
}

// ========= MAIN =========
func main() {

	gpioPrep()

	time.Sleep(3 * time.Second)

	fmt.Println("TinyGo benchmark started")
	fmt.Println("benchmark,mean_us")

	for i := 0; i < RUNS_PER_BENCH; i++ {

		fmt.Printf("busy_loop,%.2f\n", benchBusyLoop())
		time.Sleep(200 * time.Millisecond)

		fmt.Printf("gpio_toggle,%.2f\n", benchGpioToggle())
		time.Sleep(200 * time.Millisecond)

		fmt.Printf("matmul,%.2f\n", benchMatmul())
		time.Sleep(200 * time.Millisecond)

		fmt.Printf("fft,%.2f\n", benchFFT())
		time.Sleep(200 * time.Millisecond)
	}

	for {
		time.Sleep(time.Second)
	}
}
