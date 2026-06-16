#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#define WIFI_SSID "Olesya"
#define WIFI_PASSWORD "olesa200524"

int main() {
    stdio_init_all();

    printf("Инициализация Wi-Fi...\n");
    if (cyw43_arch_init()) {
        printf("Ошибка инициализации Wi-Fi\n");
        return -1;
    }

    printf("Включение режима STA (клиент)\n");
    cyw43_arch_enable_sta_mode();

    printf("Подключение к Wi-Fi SSID: %s\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Ошибка подключения к Wi-Fi\n");
        return -1;
    }

    printf("Wi-Fi подключен! Можно дальше работать.\n");

    while (true) {
        sleep_ms(1000);
    }

    return 0;
}