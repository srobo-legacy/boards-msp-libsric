#include <stdint.h>
#include <string.h>
#include "version-buf.h"
#include "version-buf-data.h"

uint8_t version_buf_read( const sric_if_t *iface )
{
	uint16_t off, remaining;
	uint8_t send;
	const uint8_t *data = iface->rxbuf + SRIC_DATA;

	if( iface->rxbuf[SRIC_LEN] != 3 )
		/* Wrong amount of data */
		return 0;

	off = data[1] | (((uint16_t)data[2]) >> 8);
	remaining = VERSIONBUF_LEN - off;

	if( off >= VERSIONBUF_LEN ) {
		send = 0;
		off = 0;
	} else if( remaining > MAX_PAYLOAD )
		send = MAX_PAYLOAD;
	else
		send = remaining;

	memcpy( iface->txbuf + SRIC_DATA, version_buf + off, send );
	return send;
}
