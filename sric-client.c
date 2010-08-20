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
#include "sric.h"

void sric_client_init( void )
{

}

uint8_t sric_client_rx( const sric_if_t *iface )
{
	/* Check that the frame's for us */
	if( sric_rxbuf[SRIC_DEST] != sric_addr )
		/* TODO: Process broadcasts */
		return 0;

	/* Command frame? */
	if( !sric_frame_is_ack( sric_rxbuf) ) {
		uint8_t *data = sric_rxbuf + SRIC_DATA;

		/* First byte of data is command byte */
		uint8_t cmd = data[0];

		if( sric_cmd_num < cmd ) {
			uint8_t len = sric_commands[cmd].cmd( iface );
			iface->rx_unlock();

			return len;
		}
	}

	/* TODO... */
	while(1);
	return 0;
}
