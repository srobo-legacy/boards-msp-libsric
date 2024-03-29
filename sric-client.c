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

#include <drivers/sched.h>
#include "version-buf.h"

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
	/* Commands required for enumeration */
	{ syscmd_reset },
	{ syscmd_enum_tok_advance },
	{ syscmd_addr_assign },
	{ syscmd_addr_info },

	/* Git version information */
	{ version_buf_read },
};

static volatile bool delay_flag = false;

static bool delay_cb( void *dummy __attribute__((unused)))
{
	delay_flag = true;
	return false;
}

const static sched_task_t delay_task = {
	.t = 1,
	.cb = delay_cb,
};

void insert_enum_delay( void )
{
	sched_add(&delay_task);

	while (delay_flag == false)
		;

	delay_flag = false;
	return;
}

#define NUM_SYSCMDS ( sizeof(syscmds) / sizeof(*syscmds) )

#define is_syscmd(x) ( x & 0x80 )
#define syscmd_num(x) ( x & ~0x80 )

void sric_client_init( void )
{

}

static uint8_t invoke( const sric_cmd_t *cmd, const sric_if_t *iface )
{
	uint8_t len = cmd->cmd( iface );

	/* Return immediately if a special error code was returned; however
	 * don't count the SRIC_RESPOND_NOW flag */
	if ((len & ~SRIC_RESPOND_NOW) >= SRIC_SPECIAL_RET_LIMIT)
		return len;

	uint8_t const *rxbuf = iface->rxbuf;

	iface->txbuf[0] = 0x7e;
	iface->txbuf[SRIC_DEST] = rxbuf[SRIC_SRC];
	iface->txbuf[SRIC_SRC] = sric_addr;
	iface->txbuf[SRIC_LEN] = len & ~SRIC_RESPOND_NOW;
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
	if( dest == 0 && is_syscmd(cmd) ) {
		uint8_t sys = syscmd_num(cmd);

		if ( len > 0 && sys < NUM_SYSCMDS ) {
			insert_enum_delay();
			return invoke( syscmds + sys, iface );
		}

		return SRIC_IGNORE;
	}

	if( len == 0 )
		return SRIC_CLIENT_INV_CMD;

	if( dest != sric_addr || sric_frame_is_ack( rxbuf ) )
		return SRIC_IGNORE;

	if( is_syscmd(cmd) ) {
		uint8_t sys = syscmd_num(cmd);

		if ( sys < NUM_SYSCMDS ) {
			insert_enum_delay();
			return invoke( syscmds + sys, iface );
		} else {
			return SRIC_CLIENT_INV_CMD;
		}
	}

	if( cmd < sric_cmd_num )
		return invoke( sric_commands + cmd, iface );

	return SRIC_CLIENT_INV_CMD;
}

/* Reset the device, move into enumeration mode */
static uint8_t syscmd_reset( const sric_if_t *iface )
{
	/* Reset the SRIC device -- entering enumeration mode */
	iface->ctl( SRIC_CTL_RESET );

	return SRIC_IGNORE;
}

/* Advance the token */
static uint8_t syscmd_enum_tok_advance( const sric_if_t *iface )
{
	/* Release it */
	iface->ctl( SRIC_CTL_RELEASE_TOK );

	/* Start using the token */
	iface->use_token(true);

	/* Respond with ACK - and immediately, without waiting for token. See
	 * srobo-devel@ traffic on 11/12/2010, we can deadlock if the first ack
	 * to a TOK_ADVANCE disappears, we send a second ack, but are already
	 * in token mode and so wait forever for a token */
	return 0 | SRIC_RESPOND_NOW;
}

/* Receive a new address from the director, and reply with info */
static uint8_t syscmd_addr_assign( const sric_if_t *iface )
{
	/* Check that we have the token */
	if( sric_conf.token_drv->have_token() ) {
		sric_addr = iface->rxbuf[SRIC_DATA + 1];

		/* Transmit device info */
		return syscmd_addr_info(iface);
	}

	/* Not for us */
	return SRIC_IGNORE;
}

/* Send reply containing info */
static uint8_t syscmd_addr_info( const sric_if_t *iface )
{
	iface->txbuf[SRIC_DATA] = sric_client_conf.devclass;

	return 1;
}
