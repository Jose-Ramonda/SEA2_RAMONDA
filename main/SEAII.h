/*
*   Archivo de tareas que se ejecutaran en funcion de demostrar arquitectura desarrollada 
*   en el marco de asignatura Sistemas embebidos avanzados dos

*   Contenidos:
*       UART
*
*   Autor: José Ramonda
*   Actualizado: 1/3/2026
*/



#pragma once

#define LED_INTEGRADO 2



void CMD2_LED_task(void *pvParameters);
void CMD3_SLOW_task(void *pvParameters);
void CMD100_INVERTER_task(void *pvParameters);