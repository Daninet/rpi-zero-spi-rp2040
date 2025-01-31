#ifndef SPI_SLAVE_H
#define SPI_SLAVE_H

#include <stdint.h>

#define SPI_MESSAGE_SIZE 128

void spi_slave_init();
void spi_queue_tx(const uint8_t* data);
int spi_get_rx(uint8_t* buffer);

#endif // SPI_SLAVE_H
