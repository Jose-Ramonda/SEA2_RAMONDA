/*
*   Archivo de cabeecera de tareas de protocolo de comunicacion RS485
*   Incluye funciones de parseo, detección de errores y formateo para envío
*   Autor: José Ramonda
*   Ultima actualización: 28/2/2026
*/
#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/queue.h"
#include "freertos/semphr.h"

//#include "config.h"



//Definiciones del protocolo

#define PROTOCOL_HEADER_SIZE 3          //Tamaño en bits del encabezado
#define PROTOCOL_START_BYTE 0xAA        //Inicio de trama de protocolo
#define PROTOCOL_MAX_PAYLOAD_SIZE 64    //Tentativo, máximo payload enviable
#define PROTOCOL_CRC_SEED 0xFFFF        //Polinomio del CRC
#define PROTOCOL_WAIT   10                // Tiempo en ms a esperar por la llegada de un byte -Ver si corresponde a protocol o al config general 

//Tiempo de polling en ms para timeout de salida
#define PROTOCOL_POLLING_TIME 10

//Bits de notificacion de mensaje
#define PROTOCOL_RECIVED_GOOD 0x01
#define PROTOCOL_RECIVED_BAD 0x02       //Este se puede desglosar despues en valores de crc y cmd desconocido

#define PROTOCOL_ACK_CMD 0
#define PROTOCOL_NACK_CMD 1

//Estados de la MEF parser

#define ST_WAIT 1               //Esperar el  bit de inicio
#define ST_VAL_ID 2             //Validar ID para ver si el msj es para uno
#define ST_LEER_ENCABEZADO 3    //Leer cmd y len
#define ST_LEER_PAYLOAD 4       //Usá la imaginación
#define ST_VAL_CRC 5            //Valida CRC
#define ST_PARS 6               //Llama a funcion/task correspondiente
#define ST_REP 7                //Si el CRC da mal manda a pedir que repita


typedef void (*dispatcher_interface_func)(uint8_t*, int);    //creo punteros a funcion que coinciden con getter y sender
typedef StreamBufferHandle_t (*parser_interface_func)(void);      

typedef struct {

    uint8_t id;
    uint8_t cmd;
    uint8_t len;
    uint8_t *payload;
    uint16_t CRC;
} msj_t;

typedef struct {

    msj_t msj;
    SemaphoreHandle_t semaforo;
} q_msj_t;

typedef struct{
    int ctrl_cmds;
    int st_cmds;
    uint8_t masterid; 
    uint8_t nodoid;

    int parser_stack;
    parser_interface_func buffer_getter;    //serian los m¿pvparameters
    int parser_priority;

    int dispatcher_stack;
    dispatcher_interface_func sender;    //serian los m¿pvparameters
    int dispatcher_priority;

}   protocol_params_t;


void composer(uint8_t cmd, uint8_t len, uint8_t *payload, SemaphoreHandle_t binsen);
void reciver_task(void* pvParameters);
void protocol_init(protocol_params_t *params);

MessageBufferHandle_t cmd_buff_getter(int id);
SemaphoreHandle_t protocol_get_ctrl_sem(int cmd);