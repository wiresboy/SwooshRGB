#ifndef HIBERNATE_H_
#define HIBERNATE_H_

#include <stdint.h>
#include "freertos/FreeRTOS.h"

void init_hibernate_io(void);
void hibernate(void);
void hibernatetask(void *pvParameters);

#endif /* HIBERNATE_H_ */
