#ifndef __CRC16_H
#define __CRC16_H
#include <stdint.h>

#define POLYNOMIAL 0xA001

uint16_t crc16( const uint8_t *data, uint8_t len );

#endif	/* __CRC16_H */
