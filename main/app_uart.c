/*
*   Este arcivo contiene las funciones/tareas relativas a la inicializacion
*   De la interefaz UART así como de la recepcion y envio de mensajes
*
*   Autor: José Ramonda
*   Actualizado 21/1/2026
*/
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/uart.h"      // UART
#include "driver/gpio.h"      // GPIO



#include "app_uart.h"
#include "config.h"
#include "protocol.h"

static StreamBufferHandle_t rx_stream = NULL;  // Global privada
static QueueHandle_t uart_cola;

void uart_init(void){       //Tarea, que se autodestruye mejor que función, la funcion perdura, la función queda

    rx_stream = xStreamBufferCreate(UART_RX_STREAMBUFFER_SIZE,1);

    uart_config_t uart_config = {       //Creamos estructura de configuraciones de uart
        .baud_rate = UART_BAUD_RATE,    //Esto viende del config
        .data_bits = UART_DATA_8_BITS,  //Palabras de 8 bits macro del idf
        .parity = UART_PARITY_DISABLE,  //Sin bit de paridad de idf
        .stop_bits = UART_STOP_BITS_1,  // 1 bit de stopp
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,  
        .source_clk = UART_SCLK_APB,
    };


    //ESP_ERROR_CHECK(uart_driver_install(UART_PORT,UART_RX_BUF_SIZE,UART_TX_BUF_SIZE,UART_EVENT_QUEUE_SIZE,NULL,0));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT,UART_RX_BUF_SIZE,UART_TX_BUF_SIZE,UART_EVENT_QUEUE_SIZE,&uart_cola,0));

    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config)); //Configurar el UART y verificar
    
   
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT,UART_TX_PIN,UART_RX_PIN,UART_RTS_PIN,UART_PIN_NO_CHANGE));   //Configuramos pines uart, solo Tx y Rx ya que los demas no se usan en rs485, son para control de flujo por hardware

    ESP_ERROR_CHECK(uart_set_mode(UART_PORT, UART_MODE_RS485_HALF_DUPLEX));
   
    ESP_LOGI("UART", "UART inicializado correctamente");
}

void uart_rx_task_poll(void *arg)
{
    // Usar static saca el buffer del stack de la tarea y lo lleva a la memoria global
    static uint8_t temp_rx_buf[UART_RX_STREAMBUFFER_SIZE];    //Buffer temporal donde se lee el buffer de rx
    ESP_LOGI("UART","TAREA DE RX");
    uart_write_bytes(UART_PORT, "ABC\n", 4);
    while (1) {
        int n = uart_read_bytes(
            UART_PORT,
            temp_rx_buf,
            100,
            //sizeof(temp_rx_buf),
            pdMS_TO_TICKS(100)
        );

        if (n > 0) {
            printf("Recibi algo\n");
            xStreamBufferSend(rx_stream, (void*) temp_rx_buf, n , 0);
        }
    }
}

StreamBufferHandle_t uart_get_rx_streambuffer(void){
    return rx_stream;
}



void uart_rx_task(void *pvParameters) {
    uart_event_t event;
    static uint8_t temp_rx_buf[UART_RX_STREAMBUFFER_SIZE];    //Buffer temporal donde se lee el buffer de rx
    ESP_LOGI("UART","TAREA DE RX");     
    uart_write_bytes(UART_PORT, "ABC\n\r", 5);    //Lo uso para debuguear el picocom se va

    while(1) {
        // 1. Esperar un evento de la cola (Bloqueo total sin CPU)
        if(xQueueReceive(uart_cola, (void *)&event, portMAX_DELAY)) {
            
            // 2. ¿Qué pasó en la UART?
            switch(event.type) {
                
                case UART_DATA:
                    // ¡Llegaron datos! 'event.size' nos dice cuántos.
                    // Leemos exactamente lo que dice el evento.
                    int n = uart_read_bytes(UART_PORT, temp_rx_buf, event.size, pdMS_TO_TICKS(10));//timeout de seguridad
                    
                    if (n > 0) {
                        // 3. Pasamanos al StreamBuffer para que la otra tarea lo procese
                        xStreamBufferSend(rx_stream, temp_rx_buf, n, 0);
                    }
                    break;

                case UART_FIFO_OVF:
                    ESP_LOGW("UART", "Hardware FIFO saturado (Overflow)");
                    uart_flush_input(UART_PORT);
                    xQueueReset(uart_cola);
                    break;

                case UART_BUFFER_FULL:
                    ESP_LOGW("UART", "Ring Buffer lleno");
                    uart_flush_input(UART_PORT);
                    xQueueReset(uart_cola);
                    break;

                case UART_BREAK:
                    //ESP_LOGI("UART", "Detección de línea en BREAK");
                    break;

                default:
                    // Otros eventos (errores de paridad, etc.)
                    break;
            }
        }
    }
    vTaskDelete(NULL); // Por buena práctica
}


void app_uart_send(uint8_t *trama, int len){
    uart_write_bytes(UART_PORT, (const char*)trama, (size_t) len);

    /*for(int i = 0; i< len; i++){    //Solo para debug
        ESP_LOGI("COPOSER", "  0x%02X ", trama[i]);
    }
        */
    // Bloqueante: asegura que el Dispatcher no siga hasta que el cable esté libre
    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(PROTOCOL_POLLING_TIME/2));   //Tiene que esperar la mitad del tiempo de polling como maximo
}