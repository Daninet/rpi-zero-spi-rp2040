import spidev
import time

def main():
    spi = spidev.SpiDev()
    
    # Open SPI port 0, with CS pin 0
    spi.open(0, 0)
    
    spi.max_speed_hz = 1000000
    spi.mode = 1 # mode 0 is not working on RP2040 in slave mode
    spi.bits_per_word = 8
    spi.lsbfirst = False

    counter = 0
    
    try:
        while True:
            test_data = bytearray([0x00] * 128)
            for i in range(128):
                test_data[i] = (i + counter) % 256

            counter += 1
            if counter == 256:
                counter = 0
            
            resp1 = spi.xfer2(test_data)
            
            print("Sent data:", [hex(x) for x in test_data[:128]])
            print("First response:", [hex(x) for x in resp1[:128]])
            print("")
            
            time.sleep(0.1)
            
    except KeyboardInterrupt:
        print("\nSPI test stopped by user")
    finally:
        # Clean up
        spi.close()
        print("SPI connection closed")

if __name__ == "__main__":
    main()
