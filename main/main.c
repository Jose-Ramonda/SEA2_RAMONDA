
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "config.h"
#include "app_uart.h"
#include "protocol.h"

#include "esp_rom_crc.h"    //muy puta de usar
#include "esp_crc.h"

void app_main(void){   
    
    int a= 3;
    uint8_t b = MASTER_ID;
    uint8_t c = NODO_ID_DEFAULT;    //No me deja llamar con macros la porquería esta
    
    uart_init();
    protocol_init(a,b,c);
    xTaskCreate(uart_rx_task2,"RX_TASK",4096,NULL,4,NULL);

    //xTaskCreate(reciver_task,"ECHO_TASK",4096,NULL,1,NULL);    
    xTaskCreate(parser_task,"PARSER_TASK",4096,NULL,1,NULL);

    xTaskCreate(dispatcher_task,"DISPATCHER",4096,NULL,2,NULL);

    printf("Arrancamos:\n\r");
    uint8_t datos[] = "HOLA"; 

    // Llamamos al composer
    // cmd = 2 (por ejemplo), len = 4, payload = datos, semáforo = NULL (no hace falta para un string estático)
    composer(2, 4, datos, NULL);


}
   

