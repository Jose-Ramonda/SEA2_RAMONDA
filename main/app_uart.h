/*
*   Archivo de cabeecera de tareas de manejo de interfaz UART
*   Autor: José Ramonda
*   Ultima actualización: 21/1/2026
*/
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"


#include "driver/uart.h"
/* UART / RS485 configuration */

#define UART_PORT          UART_NUM_1
#define UART_BAUD_RATE     9600

#define UART_TX_PIN        4
#define UART_RX_PIN        5
#define UART_RTS_PIN       18   // DE/RE del transceiver RS485

#define UART_RX_BUF_SIZE   2048
#define UART_TX_BUF_SIZE   0    // sin buffer TX

#define UART_EVENT_QUEUE_SIZE    10   //Tamaño de la cola de eventos UART
#define UART_RX_STREAMBUFFER_SIZE  2048



void uart_init(void);
void uart_rx_task(void *pvParameters);

void app_uart_send(uint8_t *trama, int len);
StreamBufferHandle_t uart_get_rx_streambuffer(void);  // Acceso público al contenido del buffer
