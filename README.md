# Communication between Raspberry Pi Zero 2W and RP2040 via SPI: Challenges and Solutions

I wanted to communicate between a Raspberry Pi Zero 2W and an RP2040 microcontroller, while having Zero 2W as the master and RP2040 as the slave. While the task initially seemed straightforward, it turned out to be more challenging than expected. I've found a lot of unanswered questions online, so I decided to share my own solution to help others facing similar issues.

- Slave mode on the RP2040 SPI peripheral seems to have bugs. I think SPI mode 0 and SPI mode 2 are not working or not standard compliant. It needs to have "SPI_CPHA_1" set when used as slave.

- Raspberry Pi Zero 2W also has a bug in the SPI1 peripheral. It only supports SPI modes 0 and 2. On the other hand, SPI0 works fine with all modes. This required me to rewire my connections.

- The RP2040 SPI pheripheral seems to not handle the chip select signal correctly in slave mode. I had to add my own GPIO interrupt handler.

- I wanted to use DMA for the SPI transfers. I ran into an issue with the data being corrupted or shifted. It turned out that the issue was with SPI TX FIFO. The DMA, given too much data, fills the whole TX FIFO. That FIFO is emptied at the next packet, leading to corrupted data streams.

- I couldn't find an efficient way to reset the SPI TX FIFO between messages. Resetting the whole SPI peripheral is too slow.

# Results

- I managed to get the bidirectional communication working.
- The SPI message length is fixed (128 bytes in my example).
- Messages seem to be in sync. I don't see any data corruption.
- The master and slave are synchronized using the SPI Chip Select signal.
- RP2040 uses DMA for the SPI transfers, so it's fast. There is also circular buffering for the RX and TX FIFOs.

<img width="1505" alt="img" src="https://github.com/user-attachments/assets/3f2a5771-a778-44cc-9e9f-37fde1b700b9" />


# Usage

```c
int main() {
  spi_slave_init();

  while (1) {
    uint8_t msg[SPI_MESSAGE_SIZE];
    if (spi_get_rx(msg)) {
      spi_queue_tx(msg);
    }
  }
}
```

# License

MIT
