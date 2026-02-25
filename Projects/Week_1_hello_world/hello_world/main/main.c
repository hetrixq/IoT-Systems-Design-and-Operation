#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    int i = 0;
    printf("Hello world!\n");
    while (1)
    {
        printf("This program runs since %d seconds.\n", i++); 
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}