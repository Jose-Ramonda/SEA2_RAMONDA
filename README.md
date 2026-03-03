El main es la carpeta de proyecto, diseñado para esp32, fase inicial, recepcion de mensajes serial implementada, envio en proceso y diámica de protocolo en proceso.
El archivo tester envía pollings periodicos cada 1 segundo y muestra la respuesta por consola (comando 0 o ACK), se puede mandar los siguientes comandos en el ciclo de polling:
1 envia el comando 2: Togglear led integrado, en el proximo ciclo de polling se recibe confirmación
2 envia el comando 3: Llama a una tarea que espera 10 segundos y confirma, para probar que no es bloqueante, en 10 ciclos llega la confirmación
3: envia comando 100 con payload, devuelve comando 100 con payload invertido
4: envia comando 100 pero con crc mal calculado, debería devolverse comando 1 NACK
