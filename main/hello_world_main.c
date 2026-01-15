#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <math.h>

#define N 1000000      // Количество итераций
#define M 10           // Количество повторов
#define ARRAY_SIZE 1000

static const char *TAG = "LoopBenchmark";

// ======== Тестовые функции ========
void empty_loop() {
    volatile int dummy;
    for (int i = 0; i < N; i++) dummy = i;
}

void int_arith_loop() {
    volatile int sum = 0;
    for (int i = 0; i < N; i++) sum += i * 3 + 7;
}

void float_arith_loop() {
    volatile double acc = 0;
    for (int i = 0; i < N; i++) acc += i * 1.2345;
}

void array_access_loop() {
    int arr[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = i;

    for (int i = 0; i < N; i++) arr[i % ARRAY_SIZE] += 1;
}

void branch_loop() {
    volatile int a = 0, b = 0;
    for (int i = 0; i < N; i++) {
        if (i % 3 == 0) a++;
        else b++;
    }
}

void func_call() {
    volatile int a = 0;
    a++;
}

void function_call_loop() {
    for (int i = 0; i < N; i++) func_call();
}

// ======== Вспомогательная функция запуска ========
void run_test(void (*test_func)(), const char* name) {
    printf("%s,", name);
    for (int j = 0; j < M; j++) {
        int64_t start = esp_timer_get_time();
        test_func();
        int64_t end = esp_timer_get_time();
        printf("%lld", end - start);
        if (j < M - 1) printf(",");
    }
    printf("\n");
}

// ======== Главная функция ========
void app_main(void) {
    printf("Test,Run1_us,Run2_us,Run3_us,Run4_us,Run5_us,Run6_us,Run7_us,Run8_us,Run9_us,Run10_us\n");

    run_test(empty_loop, "Empty loop");
    run_test(int_arith_loop, "Integer arithmetic");
    run_test(float_arith_loop, "Floating-point arithmetic");
    run_test(array_access_loop, "Array access");
    run_test(branch_loop, "Branch-heavy");
    run_test(function_call_loop, "Function call");

    printf("Benchmark finished.\n");
}

