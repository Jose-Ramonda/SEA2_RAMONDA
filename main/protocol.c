/*
*   Este arcivo contiene las funciones/tareas relativas a la clasificación de
*   tramas recibidas por RS485, su verificación y el formateo para el envío
*
*   Autor: José Ramonda
*   Actualizado 21/1/2026
*/


#include <stdio.h>
#include <string.h>

/* 1. FreeRTOS - EL ORDEN AQUÍ ES VITAL */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"
#include "freertos/event_groups.h"


/* 2. Cabeceras del Sistema / SDK */
#include "esp_log.h"
#include "esp_rom_crc.h"    //muy puta de usar
#include "esp_crc.h"
#include "driver/uart.h"
#include "driver/gpio.h"

/* 3. Tus cabeceras locales */
#include "config.h"
#include "app_uart.h"
#include "protocol.h"

static uint8_t id_nodo;
static uint8_t id_master;

static QueueHandle_t tx_queue; // Solo visible en este archivo

static MessageBufferHandle_t cmd_buff[PROTOCOL_MAX_COMANDS];

void protocol_init(int cmds, uint8_t masterid, uint8_t nodoid){
    id_nodo = nodoid;
    //id_master = masterid;
    id_master = nodoid;

    if(cmds > PROTOCOL_MAX_COMANDS) cmds = PROTOCOL_MAX_COMANDS;
    for(int i=0;i < cmds ;i++){
        cmd_buff[i]= xMessageBufferCreate((size_t)PROTOCOL_MAX_PAYLOAD_SIZE);
        if (cmd_buff[i] == NULL) {
            ESP_LOGE("PROTOCOL", "Error creando buffer %d", i);
        }
    }

       tx_queue = xQueueCreate((UBaseType_t)cmds, (UBaseType_t)sizeof(q_msj_t));    //conservador
}

MessageBufferHandle_t cmd_buff_getter(int id){
    if (id < 0 || id >= PROTOCOL_MAX_COMANDS) return NULL;
    return cmd_buff[id];
}

void parser_task(void *pvParameters) { // Recibe los datos entrantes, clasifica y valida
    StreamBufferHandle_t rx_stream = uart_get_rx_streambuffer();    //Despeus desacoplar

    if (rx_stream == NULL) {    //Verifica la existencia del buffer
        ESP_LOGE("PROTOCOL", "El buffer no existe, abortando");
        vTaskDelete(NULL);
    }

    uint8_t byte_in;
    uint8_t buff[PROTOCOL_MAX_PAYLOAD_SIZE + PROTOCOL_HEADER_SIZE];   //Aquí se guardan los datos recibidos ¿usar variables y malloc?
    uint8_t CRC[2];
    
    uint8_t cmd = -1;   //valor de error para evitar un falso comando
    uint8_t len =0; //En teoria el valor no afecta cambia pero el compilador exije inicializar

    uint16_t crc_calc;
    uint16_t crc_recibido;

    int estado = ST_WAIT;
    while (1) {

        switch (estado)
        {
        case ST_WAIT:
            xStreamBufferReceive(rx_stream, &byte_in, 1, portMAX_DELAY);    //Bloquea hasta recibir un dato
            if (byte_in == PROTOCOL_START_BYTE) estado = ST_VAL_ID;         //Si coincide el start cambia
            break;                                                          //Vuelve a evaluar
        
        case ST_VAL_ID:
            if(xStreamBufferReceive(rx_stream, &byte_in, 1, pdMS_TO_TICKS(PROTOCOL_WAIT)) == 1){    //Si recibo el byte
                if (byte_in == id_nodo){
                    buff[0] = byte_in;  //guardo el dato
                    estado = ST_LEER_ENCABEZADO;                                                // y coincide el id sigo
                } else estado = ST_WAIT;                                                              //si no coincide salgo
            } else{
                ESP_LOGI("PARSER","TIMEOUT");                                                           // si no llega nada
                estado = ST_WAIT;
            } 
            break;


        case ST_LEER_ENCABEZADO:        //Esta es el bloque mas vulnerable a cambios si cambia el protocolo
            if(xStreamBufferReceive(rx_stream, &buff[1], 2, pdMS_TO_TICKS(PROTOCOL_WAIT * 2)) == 2){
                cmd = buff[1];  //robusto?
                len = buff[2];
                if (len > 0){
                    estado = ST_LEER_PAYLOAD;
                } else {
                    estado = ST_VAL_CRC;
                }
            } else{
                ESP_LOGI("PARSER","TIMEOUT");                                            // si no llega nada
                estado = ST_WAIT;
            } 
            break;

        case ST_LEER_PAYLOAD:
            if(xStreamBufferReceive(rx_stream,&buff[PROTOCOL_HEADER_SIZE], len, pdMS_TO_TICKS(PROTOCOL_WAIT * len))){
                estado = ST_VAL_CRC;
            } else{
                ESP_LOGI("PARSER","TIMEOUT");                                            // si no llega nada
                estado = ST_WAIT;
            } 
            break;
        
        case ST_VAL_CRC:
            if(xStreamBufferReceive(rx_stream, CRC, 2, pdMS_TO_TICKS(PROTOCOL_WAIT * 2)) == 2){
                    crc_calc = esp_crc16_le(PROTOCOL_CRC_SEED, buff, len + PROTOCOL_HEADER_SIZE);
                    crc_recibido = (CRC[1] << 8) | CRC[0];
                    if (crc_calc == crc_recibido){
                        estado = ST_PARS;
                    } else {
                        ESP_LOGE("PARSER","CRC INVALIDO");
                        estado = ST_REP;
                    }
                } else{
                    ESP_LOGI("PARSER","TIMEOUT");                                            // si no llega nada
                    estado = ST_WAIT;
                } 
                break;
        case ST_PARS:
            //Acá tengo que hacer el parseo
            ESP_LOGI("PARSER","MENSAJE EXITOSO, Comando %d Msj len %d", cmd, len);
            printf("%.*s\n", len, &buff[PROTOCOL_HEADER_SIZE]);
            //Aca debo mandar el ACK, no en las tareas
            xMessageBufferSend(cmd_buff[cmd],&buff[PROTOCOL_HEADER_SIZE],len,pdMS_TO_TICKS(PROTOCOL_WAIT * len));
            estado = ST_WAIT;
            break;
        case ST_REP:
             //Acá mando la solcitud de repe cuadno haga el composer, el NACK
            break;
            
        default:
            ESP_LOGE("PARSER","ERROR EN MAQUINA DE ESTADOS - ESTADO INDEFINIDO");
            break;
        }  
    }
}

void composer(uint8_t cmd, uint8_t len, uint8_t *payload, SemaphoreHandle_t binsen){
    /*Toma semaforo de puntero a payload y encola los datos*/
    ESP_LOGI("COMPOSER","ARRNACA COMPSER");
    q_msj_t mensaje;
    uint16_t crc;  //mas adelante cambiar todo al crc de 16 bits, mas facil en general aunque menos legible
    
    mensaje.msj.id = id_master;
    mensaje.msj.cmd = cmd;
    mensaje.msj.len = len;
    mensaje.msj.payload = payload;
    mensaje.semaforo = binsen;

    if(binsen != NULL){     //Bloqueo escritura del buffer antes de calcular el crc para asegurar integridad (como es func y es secuencial deberia ser lo mismo)
        xSemaphoreTake(binsen, portMAX_DELAY); 
    }

    crc = esp_crc16_le(PROTOCOL_CRC_SEED, (uint8_t*)&mensaje.msj.id, sizeof(uint8_t));
    crc = esp_crc16_le(crc, (uint8_t*)&mensaje.msj.cmd, sizeof(uint8_t));
    crc = esp_crc16_le(crc, (uint8_t*)&mensaje.msj.len, sizeof(uint8_t));

    if(payload != NULL && len>0){
        crc = esp_crc16_le(crc, payload, len);
    }
    
    mensaje.msj.CRC[0] = (uint8_t)(crc >> 8);
    mensaje.msj.CRC[1] = (uint8_t)(crc& 0xFF);

    //Justo antes de ma ndar tomo el semáforo, así aseguro que si el dispatcher es de mas prioridad y se activa apenas mando ya este tomado

    if(xQueueSend(tx_queue, &mensaje, pdMS_TO_TICKS(PROTOCOL_WAIT) * sizeof(q_msj_t)) != pdPASS){
        ESP_LOGE("PROTOCOL", "Error: Cola de salida llena");
        if(binsen != NULL){     // si falla la cola devuelcvo el semaforo antes de volver
    }
    }


}

void dispatcher_task(void *pvParameters) {

    /*Toma datos encolados y los madna cuando recibe la notificacion de polling, tambien gestiona el envio de ack
    implementació pendiente*/

    q_msj_t mensaje;
    uint8_t buff[1+PROTOCOL_HEADER_SIZE+PROTOCOL_MAX_PAYLOAD_SIZE+sizeof(uint16_t)];
    int real_len;

    buff[0] = PROTOCOL_START_BYTE;

    while(1){
        if(xQueueReceive(tx_queue, &mensaje, portMAX_DELAY)){
            buff[1] = mensaje.msj.id;
            buff[2] = mensaje.msj.cmd;
            buff[3] = mensaje.msj.len;
            memcpy(&buff[1 + PROTOCOL_HEADER_SIZE], mensaje.msj.payload, mensaje.msj.len);
            memcpy(&buff[1 + PROTOCOL_HEADER_SIZE +  mensaje.msj.len], mensaje.msj.CRC, sizeof(uint16_t));

            real_len = 1 + PROTOCOL_HEADER_SIZE + mensaje.msj.len +sizeof(uint16_t);

            app_uart_send(buff, real_len);  //Esto despues desacoplar pro ahora solo probando funcionalidad de mensajes

            if( mensaje.semaforo != NULL){
                xSemaphoreGive(mensaje.semaforo);
            }

        }
    }

}






void reciver_task(void *pvParameters) {     //trea de prueba para debug
    StreamBufferHandle_t rx_stream = uart_get_rx_streambuffer();

    if (rx_stream == NULL) {
        ESP_LOGE("PROTOCOL", "El buffer no existe, abortando");
        vTaskDelete(NULL);
    }

    uint8_t byte_in;
    uint8_t trama[PROTOCOL_MAX_PAYLOAD_SIZE + 3];    //64 + 3 de encabezado tentativo solo pruebas
    uint8_t CRC[2];
    ESP_LOGI("PROTOCOL","TAREA DE ECHO INICIADA");


    while (1) {
        if (xStreamBufferReceive(rx_stream, &byte_in, 1, portMAX_DELAY) == 1) {

            if(byte_in == PROTOCOL_START_BYTE){
                ESP_LOGI("UART_RX", "DEC: %3d | HEX: 0x%02X | ASCII: '%c'", byte_in, byte_in, byte_in);
                xStreamBufferReceive(rx_stream, trama, 3, portMAX_DELAY);
                ESP_LOGI("UART_RX" ," ID: 0x%02X ", trama[0]);//Logueo encabezado
                ESP_LOGI("UART_RX" ," ORDEN: 0x%02X ", trama[1]);
                ESP_LOGI("UART_RX" ," LEN: 0x%02X ", trama[2]);

                xStreamBufferReceive(rx_stream, &trama[3],trama[2],pdMS_TO_TICKS(1000)); //seguimos cargando los datos

                printf("Payload: \n");  //mandamos la trama
                for(int i = 0; i < trama[2]; i++) {
                    printf("%c", trama[3 + i]); // Imprime cada letra desde el índice 3
                }
                printf("\n");

                xStreamBufferReceive(rx_stream, CRC, 2, portMAX_DELAY); //Recibo el CRC
                ESP_LOGI("UART_RX", "CRC Recibido: 0x%02X 0x%02X", CRC[0],CRC[1]); //Lo logueo

                uint16_t crc_calc = esp_crc16_le(PROTOCOL_CRC_SEED, trama, trama[2]+3);//no casteo porque trama es uint8 tamaño en 2 + encabezado
                
                

                uint8_t crc_bajo = (uint8_t)(crc_calc & 0xFF);         // Te quedas con el "D5"
                uint8_t crc_alto = (uint8_t)((crc_calc >> 8) & 0xFF);  // Movés el "1F" a la derecha y te quedás con él

                // Ahora logueas los dos para comparar con los que recibiste
                ESP_LOGI("CRC_DEBUG", "Calculado: 0x%02X 0x%02X", crc_bajo, crc_alto);

                if (crc_bajo == CRC[0] && crc_alto == CRC[1]) {
                    ESP_LOGI("PROTOCOLO", "¡EXITO! El CRC coincide.");
                } else {
                    ESP_LOGE("PROTOCOLO", "ERROR: Los bytes no son iguales.");
                }
            }

            // Log Triple: Decimal, Hexadecimal y ASCII
            //ESP_LOGI("UART_RX", "DEC: %3d | HEX: 0x%02X | ASCII: '%c'", byte_in, byte_in, byte_in);

            // Echo: reenvío por hardware
            //uart_write_bytes(UART_PORT, (const char*)&byte_in, 1);    //ya no interesa el echo
        }
    } 
} 

