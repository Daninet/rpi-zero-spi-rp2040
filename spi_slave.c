#include "spi_slave.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "pico/binary_info.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SPI_RX_PIN 12
#define SPI_TX_PIN 11
#define SPI_SCK_PIN 10
#define SPI_CSN_PIN 13

static int dma_rx_channel_data;
static int dma_tx_channel_data;

#define CIRCULAR_BUFFER_SIZE 32

static uint8_t tx_zero_buffer[SPI_MESSAGE_SIZE] __attribute__((aligned(8)));
static uint8_t tx_circular_buf[CIRCULAR_BUFFER_SIZE][SPI_MESSAGE_SIZE] __attribute__((aligned(8)));
static volatile uint8_t tx_currently_reading = 0;
static volatile uint8_t tx_stored = 0;

static uint8_t rx_circular_buf[CIRCULAR_BUFFER_SIZE][SPI_MESSAGE_SIZE] __attribute__((aligned(8)));
static volatile uint8_t rx_currently_writing = 0;
static volatile uint8_t rx_stored = 0;

critical_section_t buffer_lock;

void dma_update_addresses()
{
    if (rx_stored < CIRCULAR_BUFFER_SIZE) {
        rx_currently_writing = (rx_currently_writing + 1) % CIRCULAR_BUFFER_SIZE;
        dma_channel_set_write_addr(dma_rx_channel_data, rx_circular_buf[rx_currently_writing], false);
        rx_stored++;
    } else {
        dma_channel_set_write_addr(dma_rx_channel_data, rx_circular_buf[rx_currently_writing], false);
    }
    dma_channel_set_trans_count(dma_rx_channel_data, SPI_MESSAGE_SIZE, false);

    if (tx_stored > 0) {
        tx_currently_reading = (tx_currently_reading + 1) % CIRCULAR_BUFFER_SIZE;
        dma_channel_set_read_addr(dma_tx_channel_data, &tx_circular_buf[tx_currently_reading][0], false);
        tx_stored--;
    } else {
        dma_channel_set_read_addr(dma_tx_channel_data, tx_zero_buffer, false);
    }
    dma_channel_set_trans_count(dma_tx_channel_data, SPI_MESSAGE_SIZE, false);
}

static inline bool spi_is_tx_fifo_empty(const spi_inst_t* spi)
{
    return (spi_get_const_hw(spi)->sr & SPI_SSPSR_TFE_BITS);
}

void cs_handler(uint gpio, uint32_t events)
{
    if (events & GPIO_IRQ_EDGE_RISE) {
        // stop DMAs
        hw_clear_bits(&spi_get_hw(spi1)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);

        dma_channel_abort(dma_rx_channel_data);
        dma_channel_abort(dma_tx_channel_data);

        // flush RX FIFO (for sync purposes)
        while (spi_is_readable(spi1)) {
            uint8_t dummy;
            spi_read_blocking(spi1, 0, &dummy, 1);
        }

        dma_update_addresses();
        dma_start_channel_mask((1u << dma_rx_channel_data) | (1u << dma_tx_channel_data));
    }

    if (events & GPIO_IRQ_EDGE_FALL) {
        // important part here
        if (!spi_is_tx_fifo_empty(spi1)) {
            // by not enabling the TX DMA for one cycle, we can empty the SPI HW FIFO
            hw_set_bits(&spi_get_hw(spi1)->dmacr, SPI_SSPDMACR_RXDMAE_BITS);
        } else {
            // generic case, both RX and TX DMAs are enabled
            hw_set_bits(&spi_get_hw(spi1)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);
        }
    }
}

void spi_slave_init()
{
    critical_section_init(&buffer_lock);

    memset(tx_zero_buffer, 0, SPI_MESSAGE_SIZE);

    uint freq = spi_init(spi1, 1000000);

    spi_set_format(spi1,
        8, // Number of bits per transfer
        SPI_CPOL_0, // Polarity (CPOL)
        SPI_CPHA_1, // Phase (CPHA)
        SPI_MSB_FIRST);

    spi_set_slave(spi1, 1);

    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);

    bi_decl(bi_3pins_with_func(SPI_RX_PIN, SPI_TX_PIN, SPI_SCK_PIN, GPIO_FUNC_SPI));
    bi_decl(bi_1pin_with_name(PICO_DEFAULT_SPI_CSN_PIN, "SPI CS"));

    // Stop the DMA (it was started by spi_init())
    hw_clear_bits(&spi_get_hw(spi1)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);

    // DMA for RX
    dma_rx_channel_data = dma_claim_unused_channel(true);
    dma_channel_config rx_config = dma_channel_get_default_config(dma_rx_channel_data);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_dreq(&rx_config, spi_get_dreq(spi1, false));

    dma_channel_configure(
        dma_rx_channel_data,
        &rx_config,
        rx_circular_buf[0],
        &spi_get_hw(spi1)->dr,
        SPI_MESSAGE_SIZE,
        false);

    // DMA for TX
    dma_tx_channel_data = dma_claim_unused_channel(true);
    dma_channel_config tx_config = dma_channel_get_default_config(dma_tx_channel_data);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    channel_config_set_dreq(&tx_config, spi_get_dreq(spi1, true));

    dma_channel_configure(
        dma_tx_channel_data,
        &tx_config,
        &spi_get_hw(spi1)->dr,
        &tx_zero_buffer,
        SPI_MESSAGE_SIZE,
        false);

    gpio_init(SPI_CSN_PIN);
    gpio_set_irq_enabled_with_callback(SPI_CSN_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &cs_handler);
}

int spi_get_rx(uint8_t* data)
{
    int ret = 0;

    critical_section_enter_blocking(&buffer_lock);
    if (rx_stored > 0) {
        int rx_index = (rx_currently_writing - rx_stored + CIRCULAR_BUFFER_SIZE) % CIRCULAR_BUFFER_SIZE;
        memcpy(data, rx_circular_buf[rx_index], SPI_MESSAGE_SIZE);
        rx_stored--;
        ret = SPI_MESSAGE_SIZE;
    }
    critical_section_exit(&buffer_lock);

    return ret;
}

void spi_queue_tx(const uint8_t* data)
{
    critical_section_enter_blocking(&buffer_lock);
    if (tx_stored < CIRCULAR_BUFFER_SIZE) {
        int tx_index = (tx_currently_reading + tx_stored) % CIRCULAR_BUFFER_SIZE;
        memcpy(tx_circular_buf[tx_index], data, SPI_MESSAGE_SIZE);
        tx_stored++;
    }
    critical_section_exit(&buffer_lock);
}
