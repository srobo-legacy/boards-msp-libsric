/*   Copyright (C) 2010 Robert Spanton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */
#include "sric-client.h"

void sric_client_init( void )
{

}

uint8_t sric_client_rx( const sric_if_t *iface )
{
	uint8_t *rxbuf = iface->rxbuf;

	/* Check that the frame's for us */
	if( rxbuf[SRIC_DEST] != sric_addr )
		/* TODO: Process broadcasts */
		return SRIC_IGNORE;

	/* Command frame? */
	if( !sric_frame_is_ack( rxbuf ) ) {
		uint8_t *data = rxbuf + SRIC_DATA;

		/* First byte of data is command byte */
		uint8_t cmd = data[0];

		if( cmd < sric_cmd_num ) {
			uint8_t len = sric_commands[cmd].cmd( iface );

			iface->txbuf[0] = 0x7e;
			iface->txbuf[SRIC_DEST] = rxbuf[SRIC_SRC];
			iface->txbuf[SRIC_SRC] = sric_addr;
			iface->txbuf[SRIC_LEN] = len;
			sric_frame_set_ack(iface->txbuf);

			return len + SRIC_HEADER_SIZE;
		}
	}

	/* Reply with some kind of globally standardised error frame! */
	return SRIC_IGNORE;
}
