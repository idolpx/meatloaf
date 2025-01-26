#ifndef IECESPIDFSPI_H
#define IECESPIDFSPI_H

#include <driver/spi_master.h>
#include <driver/sdspi_host.h>

#define MSBFIRST  0
#define LSBFIRST  1

#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

class SPISettings
{
public:
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) :_clock(clock), _bitOrder(bitOrder), _dataMode(dataMode) {}
    uint32_t _clock;
    uint8_t  _bitOrder;
    uint8_t  _dataMode;
};


class MySPIClass
{
public:
  MySPIClass();
  ~MySPIClass();
  bool begin(int8_t sck, int8_t miso, int8_t mosi, SPISettings settings);
  void end();
  
  void beginTransaction();
  void endTransaction();
  uint16_t transfer16(uint16_t data);
  
private:
  bool m_initialized;
  spi_device_handle_t m_spi;
  static spi_transaction_t m_trans;
};


DMA_ATTR spi_transaction_t MySPIClass::m_trans;

MySPIClass::MySPIClass()
{
  m_initialized = false;
}


MySPIClass::~MySPIClass()
{
  end();
}


bool MySPIClass::begin(int8_t sck, int8_t miso, int8_t mosi, SPISettings settings)
{
  if( !m_initialized ) 
    {
      spi_bus_config_t buscfg;
      memset(&buscfg, 0, sizeof(buscfg));
      buscfg.miso_io_num = miso;
      buscfg.mosi_io_num = mosi;
      buscfg.sclk_io_num = sck;
      buscfg.quadwp_io_num = -1;
      buscfg.quadhd_io_num = -1;
      buscfg.max_transfer_sz = 4;
      esp_err_t res = spi_bus_initialize(SDSPI_DEFAULT_HOST, &buscfg, SPI_DMA_CH_AUTO);
      if( res==ESP_OK || res==ESP_ERR_INVALID_STATE /* SPI bus is already initialized */ )
        {
          spi_device_interface_config_t devcfg;
          memset(&devcfg, 0, sizeof(devcfg));
          devcfg.clock_speed_hz = settings._clock;
          devcfg.mode = settings._dataMode;
          devcfg.flags = settings._bitOrder==MSBFIRST ? 0 : SPI_DEVICE_BIT_LSBFIRST;
          devcfg.spics_io_num = -1;
          devcfg.queue_size = 4;
          res = spi_bus_add_device(SDSPI_DEFAULT_HOST, &devcfg, &m_spi);
          if( res==ESP_OK ) 
            m_initialized = true;
          else
            printf("spi_bus_add_device error: %i\r\n", (int) res);
        }
      else 
        printf("spi_bus_initialize error: %i\r\n", (int) res);

      memset(&m_trans, 0, sizeof(m_trans));
    }

  return m_initialized;
}


void MySPIClass::end()
{
  spi_bus_remove_device(m_spi);
  m_initialized = false;
}


void MySPIClass::beginTransaction()
{
  if( m_initialized ) 
    spi_device_acquire_bus(m_spi, portMAX_DELAY);
}


void MySPIClass::endTransaction()
{
  if( m_initialized ) 
    spi_device_release_bus(m_spi);
}

uint16_t MySPIClass::transfer16(uint16_t data)
{
  if( m_initialized )
    {
      m_trans.tx_data[0] = data >> 8;
      m_trans.tx_data[1] = data & 0xFF;
      m_trans.length = 16;
      m_trans.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
      spi_device_polling_transmit(m_spi, &m_trans);
      return m_trans.rx_data[1];
    }
  else
    return 0;
}


static MySPIClass SPI;

#endif
