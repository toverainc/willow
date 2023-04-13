#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "SALLOW";
xQueueHandle q_irq;


static void IRAM_ATTR irqh_gpio(void *args)
{
    int gpio = (int) args;
    xQueueSendFromISR(q_irq, &gpio, NULL);
}

void task_gpio(void *args)
{
    int gpio = -1;

    while (1) {
        if (xQueueReceive(q_irq, &gpio, portMAX_DELAY)) {
            printf("GPIO '%d' toggled\n", gpio);
        }
    }
}

void set_gpio_in(int gpio)
{
    gpio_pad_select_gpio(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_pulldown_en(gpio);
    gpio_pullup_dis(gpio);
    gpio_set_intr_type(gpio, GPIO_INTR_POSEDGE);
}

void app_main() {
    // BOOT/CONFIG button on GPIO0
    set_gpio_in(0);
    // MUTE button on GPIO1
    set_gpio_in(1);
    // PIR motion sensor on GPIO14
    set_gpio_in(14);

    q_irq = xQueueCreate(10, sizeof(int));
    xTaskCreate(task_gpio, "task_gpio", 2048, NULL, 1, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(0, irqh_gpio, (void *)0);
    gpio_isr_handler_add(1, irqh_gpio, (void *)1);
    gpio_isr_handler_add(14, irqh_gpio, (void *)14);
}