/*
*   Este arcivo contiene las funciones/tareas relativas a la clasificación de
*   tramas recibidas por RS485, su verificación y el formateo para el envío
*
*   Autor: José Ramonda
*   Actualizado 28/2/2026
*/


#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"
#include "freertos/semphr.h"


#include "esp_log.h"
#include "esp_crc.h"
//#include "driver/uart.h"
//#include "driver/gpio.h"

/* 3. Tus cabeceras locales */
#include "config.h"
#include "app_uart.h"
#include "protocol.h"

static uint8_t id_nodo;
static uint8_t id_master;


static int n_st_cmd;
static int n_ctrl_cmd;
static QueueHandle_t tx_queue; // Solo visible en este archivo
static MessageBufferHandle_t *cmd_buff = NULL;  //Puntero a arreglo de msjbuffers
static SemaphoreHandle_t *cmd_smph = NULL;


static TaskHandle_t parser_handler;
static TaskHandle_t dispatcher_handler;

void parser_task(void *pvParameters);   //Evitar error de implicit declaration
void dispatcher_task(void *pvParameters);


void protocol_init(protocol_params_t *params){
    id_nodo = params->nodoid;
    id_master = params->masterid;


    //Valido que los control commands esten en el rango 0-99 y los stream commands en 100-255
    if(params->ctrl_cmds > 99){
        params->ctrl_cmds = 99;
        ESP_LOGE("PROTOCOL","CANTIDAD DE COMANDOS DE CONTROL EXCEDIDA- SE AJUSTAN A 99");
    }
    if(params->st_cmds > 155){
        params->st_cmds = 155;
        ESP_LOGE("PROTOCOL","CANTIDAD DE COMANDOS DE TRANSMICION EXCEDIDA- SE AJUSTAN A 155");
    }
    n_ctrl_cmd = params->ctrl_cmds;
    n_st_cmd = params->st_cmds;

    //Reservo memmoria para los buffers, no creo un arreglo porque no me deja crear un arreglo static en funcion
    cmd_buff = (MessageBufferHandle_t *) malloc(params->st_cmds * sizeof(MessageBufferHandle_t)); 

    for(int i=0;i < params->st_cmds ;i++){
        cmd_buff[i]= xMessageBufferCreate((size_t)PROTOCOL_MAX_PAYLOAD_SIZE);
        if (cmd_buff[i] == NULL) {
            ESP_LOGE("PROTOCOL", "Error creando buffer %d", i);
        }
    }     

    //Hago lo propio para las notificaciones de lso de control
    cmd_smph = (SemaphoreHandle_t *)malloc(params->ctrl_cmds* sizeof(SemaphoreHandle_t));
    for(int j=0;j < params->ctrl_cmds ;j++){
        cmd_smph[j]= xSemaphoreCreateBinary();
        if (cmd_smph[j] == NULL) {
            ESP_LOGE("PROTOCOL", "Error creando semaforo %d", j);
        }
    }   

    //Y finalmente creo la cola de salida
    tx_queue = xQueueCreate((UBaseType_t) (params->ctrl_cmds+params->st_cmds), (UBaseType_t)sizeof(q_msj_t));  



    //Se crean las tareas
    xTaskCreate(parser_task,"PARSER_TASK",params->parser_stack,params->buffer_getter,params->parser_priority,&parser_handler);
    xTaskCreate(dispatcher_task,"DISPATCHER_TASK",params->dispatcher_stack,params->sender,params->dispatcher_priority,&dispatcher_handler);
}

MessageBufferHandle_t cmd_buff_getter(int id){
    if (id < 100 || id >= 100+n_st_cmd || cmd_buff == NULL) return NULL;
    return cmd_buff[id-100];
}


SemaphoreHandle_t protocol_get_ctrl_sem(int cmd) {
    if (cmd < n_ctrl_cmd && cmd_smph != NULL) {
        return cmd_smph[cmd];
    }
    return NULL;
}

void parser_task(void *pvParameters) { // Recibe los datos entrantes, clasifica y valida
    // Casteamos el puntero genérico al tipo de función definida
    parser_interface_func getter = (parser_interface_func) pvParameters;

    // La ejecutamos para obtener el StreamBuffer
    StreamBufferHandle_t rx_stream = getter();    

    if (rx_stream == NULL) {    //Verifica la existencia del buffer
        ESP_LOGE("PROTOCOL", "El buffer no existe, abortando");
        vTaskDelete(NULL);
    }

    uint8_t byte_in;
    uint8_t buff[PROTOCOL_MAX_PAYLOAD_SIZE + PROTOCOL_HEADER_SIZE];   //Aquí se guardan los datos recibidos ¿usar variables y malloc?

    
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
            if(xStreamBufferReceive(rx_stream, &crc_recibido, 2, pdMS_TO_TICKS(PROTOCOL_WAIT * 2)) == 2){
                    crc_calc = esp_crc16_le(PROTOCOL_CRC_SEED, buff, len + PROTOCOL_HEADER_SIZE);
                    
                    if (crc_calc == crc_recibido){
                        estado = ST_PARS;
                    } else {
                        ESP_LOGI("PARSER","CRC INVALIDO");
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
            if(cmd >= 100 && cmd-100 < n_st_cmd && cmd_buff != NULL){
                xMessageBufferSend(cmd_buff[cmd-100],&buff[PROTOCOL_HEADER_SIZE],len,pdMS_TO_TICKS(PROTOCOL_WAIT * len));
            }
            if(cmd<100 && cmd < n_ctrl_cmd && cmd_smph != NULL){
                xSemaphoreGive(cmd_smph[cmd]);
            }

            xTaskNotify(dispatcher_handler, PROTOCOL_RECIVED_GOOD, eSetValueWithOverwrite); //informo evento exitoso
            estado = ST_WAIT;
            break;
        case ST_REP:
             xTaskNotify(dispatcher_handler, PROTOCOL_RECIVED_BAD, eSetValueWithOverwrite);
             estado = ST_WAIT;
            break;
            
        default:
            ESP_LOGE("PARSER","ERROR EN MAQUINA DE ESTADOS - ESTADO INDEFINIDO");
            break;
        }  
    }
}

void composer(uint8_t cmd, uint8_t len, uint8_t *payload, SemaphoreHandle_t binsen){
    /*Toma semaforo de puntero a payload y encola los datos*/
    //ESP_LOGI("COMPOSER","ARRNACA COMPSER");
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
    
    mensaje.msj.CRC = crc;//[0] = (uint8_t)(crc >> 8);
    //mensaje.msj.CRC[1] = (uint8_t)(crc& 0xFF);

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

    dispatcher_interface_func enviar = (dispatcher_interface_func) pvParameters;

    q_msj_t mensaje;
    uint8_t buff[1+PROTOCOL_HEADER_SIZE+PROTOCOL_MAX_PAYLOAD_SIZE+sizeof(uint16_t)];
    int real_len;

    buff[0] = PROTOCOL_START_BYTE;
    uint32_t valor;

    uint16_t CRC;
    while(1){
        
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &valor, portMAX_DELAY) == pdPASS){

            switch (valor)
            {
            case PROTOCOL_RECIVED_GOOD:
                if(xQueueReceive(tx_queue, &mensaje, 0)){   //si tengo datos tatata
                    buff[1] = mensaje.msj.id;
                    buff[2] = mensaje.msj.cmd;
                    buff[3] = mensaje.msj.len;
                    memcpy(&buff[1 + PROTOCOL_HEADER_SIZE], mensaje.msj.payload, mensaje.msj.len);
                    memcpy(&buff[1 + PROTOCOL_HEADER_SIZE +  mensaje.msj.len], &mensaje.msj.CRC, sizeof(uint16_t));

                    real_len = 1 + PROTOCOL_HEADER_SIZE + mensaje.msj.len +sizeof(uint16_t);

                    enviar(buff, real_len);  //Esto despues desacoplar pro ahora solo probando funcionalidad de mensajes

                    if( mensaje.semaforo != NULL){
                        xSemaphoreGive(mensaje.semaforo);
                    }
                } else {
                    buff[1] = id_nodo;
                    buff[2] = PROTOCOL_ACK_CMD;
                    buff[3] = 0;
                    CRC = esp_crc16_le(PROTOCOL_CRC_SEED, &buff[1],PROTOCOL_HEADER_SIZE);
                    memcpy(&buff[1 + PROTOCOL_HEADER_SIZE], &CRC, sizeof(uint16_t));
                    real_len = 1+PROTOCOL_HEADER_SIZE+sizeof(uint16_t);
                    enviar(buff, real_len);  //MAndo un  simple ack
                }
                break;
            case PROTOCOL_RECIVED_BAD:
                buff[1] = id_nodo;
                buff[2] = PROTOCOL_NACK_CMD;
                buff[3] = 0;
                CRC = esp_crc16_le(PROTOCOL_CRC_SEED, &buff[1],PROTOCOL_HEADER_SIZE);
                memcpy(&buff[1 + PROTOCOL_HEADER_SIZE], &CRC, sizeof(uint16_t));
                real_len = 1+PROTOCOL_HEADER_SIZE+sizeof(uint16_t);
                enviar(buff, real_len);  //MAndo un  simple ack
                break;

            default:
                ESP_LOGE("PROTOCOL","Notificacion desconocida");
                break;
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

