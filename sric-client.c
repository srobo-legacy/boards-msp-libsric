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

/* Reset the device, move into enumeration mode */
static uint8_t syscmd_reset( const sric_if_t *iface );

/* Advance the token */
static uint8_t syscmd_enum_tok_advance( const sric_if_t *iface );

/* Receive a new address from the director, and reply with info */
static uint8_t syscmd_addr_assign( const sric_if_t *iface );

/* Send reply containing info */
static uint8_t syscmd_addr_info( const sric_if_t *iface );

/* Table of 'system' commands.
   The order of these is important! */
static const sric_cmd_t syscmds[] =
{
	{ syscmd_reset },
	{ syscmd_enum_tok_advance },
	{ syscmd_addr_assign },
	{ syscmd_addr_info },
};

#define NUM_SYSCMDS ( sizeof(syscmds) / sizeof(*syscmds) )

#define is_syscmd(x) ( x & 0x80 )
#define syscmd_num(x) ( x & ~0x80 )

void sric_client_init( void )
{

}

static uint8_t invoke( const sric_cmd_t *cmd, const sric_if_t *iface )
{
	uint8_t len = cmd->cmd( iface );
	uint8_t const *rxbuf = iface->rxbuf;

	iface->txbuf[0] = 0x7e;
	iface->txbuf[SRIC_DEST] = rxbuf[SRIC_SRC];
	iface->txbuf[SRIC_SRC] = sric_addr;
	iface->txbuf[SRIC_LEN] = len;
	sric_frame_set_ack(iface->txbuf);

	return len + SRIC_HEADER_SIZE;
}

/* TODO: Add a standard mechanism for returning error */
#define SRIC_CLIENT_INV_CMD SRIC_IGNORE

uint8_t sric_client_rx( const sric_if_t *iface )
{
	uint8_t const *rxbuf = iface->rxbuf;
	uint8_t const *data = rxbuf + SRIC_DATA;
	const uint8_t dest = rxbuf[SRIC_DEST];
	const uint8_t len = rxbuf[SRIC_LEN];

	/* First byte of data is command byte */
	const uint8_t cmd = data[0];

	/* Broadcast: */
	if( dest == 0
	    && is_syscmd(cmd) ) {
		/* We only process syscmds through broadcast */
		uint8_t sys = syscmd_num(cmd);

		if ( len > 0 && sys < NUM_SYSCMDS )
			invoke( syscmds + sys, iface );

		/* There's always no response to broadcasts */
		return SRIC_IGNORE;
	}

	if( len == 0 )
		return SRIC_CLIENT_INV_CMD;

	if( dest != sric_addr || sric_frame_is_ack( rxbuf ) )
		return SRIC_IGNORE;

	if( is_syscmd(cmd) ) {
		uint8_t sys = syscmd_num(cmd);

		if ( sys < NUM_SYSCMDS )
			return invoke( syscmds + sys, iface );
		else
			return SRIC_CLIENT_INV_CMD;
	}

	if( cmd < sric_cmd_num )
		return invoke( sric_commands + cmd, iface );

	return SRIC_CLIENT_INV_CMD;
}

/* Reset the device, move into enumeration mode */
static uint8_t syscmd_reset( const sric_if_t *iface )
{
	return 0;
}

/* Advance the token */
static uint8_t syscmd_enum_tok_advance( const sric_if_t *iface )
{
	return 0;
}

/* Receive a new address from the director, and reply with info */
static uint8_t syscmd_addr_assign( const sric_if_t *iface )
{
	return 0;
}

/* Send reply containing info */
static uint8_t syscmd_addr_info( const sric_if_t *iface )
{
	return 0;
}
