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
#include "sric-gw.h"
#include <string.h>

#if SRIC_DIRECTOR
#include "token-dir.h"
#endif

/* Events that can influence the state machine */
typedef enum {
	/* Received a frame from the host */
	EV_HOST_RX,
	/* Finished transmitting a frame to the host */
	EV_HOST_TX_COMPLETE,
	/* Received a frame from SRIC */
	EV_SRIC_RX,
	/* Finished transmitting a frame over SRIC */
	EV_SRIC_TX_COMPLETE,
	/* SRIC interface experienced an error */
	EV_SRIC_ERROR,
} gw_event_t;

typedef enum {
	IH_IDLE,
	IH_TRANSMITTING,
} inhost_state_t;

typedef enum {
	IS_IDLE,
	IS_TRANSMITTING,
} insric_state_t;

static inhost_state_t gw_inhost_state;
static insric_state_t gw_insric_state;

void sric_gw_init( void )
{
}

static bool gw_fwd_to_sric()
{
	/* Transmit frame on SRIC */
	memcpy( sric_txbuf, hostser_rxbuf, hostser_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );
	hostser_rx_done();

	sric_if.tx_lock();
	sric_if.tx_cmd_start( hostser_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE,
			      /* Avoid SRIC IF rotating by not expecting a response */
			      false );
	return true;
}

#define require_len(x) do { if( hostser_rxbuf[SRIC_LEN] != x ) return false; } while(0)

static bool gw_proc_host_cmd()
{
	uint8_t len = hostser_rxbuf[SRIC_LEN];
	uint8_t *data = hostser_rxbuf + SRIC_DATA;

	if( len == 0 )
		return false;

	if( gw_insric_state != IS_IDLE )
		/* Ignore when we can't transmit a response */
		return false;

	hostser_txbuf[0] = 0x8e;
	hostser_txbuf[SRIC_DEST] = 1 | 0x80; /* Destination is bus director */
					/* Additionally, this is an ack */
	hostser_txbuf[SRIC_SRC] = 0;	/* XXX - what's an appropriate addr
					 * for the gateway to send msgs from? */
	hostser_txbuf[SRIC_LEN] = 0;

	switch( data[0] )
	{
	case GW_CMD_USE_TOKEN:
		require_len(2);
		sric_if.use_token( data[1] ? true : false );
		break;

	case GW_CMD_REQ_TOKEN:
		require_len(1);
		sric_if.ctl( SRIC_CTL_REQUEST_TOK );
		break;

	case GW_CMD_HAVE_TOKEN:
		require_len(1);
		hostser_txbuf[SRIC_LEN] = 1;
		hostser_txbuf[SRIC_DATA] = sric_conf.token_drv->have_token();
		break;

#if SRIC_DIRECTOR
	case GW_CMD_GEN_TOKEN:
		require_len(1);
		token_dir_emit_first();
		break;
#endif
	}

	hostser_rx_done();

	/* Yes, modifying the state is naughty, but fun... */
	gw_insric_state = IS_TRANSMITTING;
	hostser_tx();
	return false;
}

/* Manages data coming in from the host */
static void gw_inhost_fsm( gw_event_t event )
{
	switch( gw_inhost_state ) {
	case IH_IDLE:
		if( event == EV_HOST_RX ) {
			bool (*f)(void) = NULL;

			if( hostser_rxbuf[0] == 0x7e )
				f = gw_fwd_to_sric;
			else if( hostser_rxbuf[0] == 0x8e )
				f = gw_proc_host_cmd;

			if( f != NULL && f() )
				gw_inhost_state = IH_TRANSMITTING;
		}
		break;

	case IH_TRANSMITTING:
		if( event == EV_SRIC_TX_COMPLETE )
			gw_inhost_state = IH_IDLE;
		break;
	}
}

/* Manages data coming in from the sric bus */
static void gw_insric_fsm( gw_event_t event )
{
	switch( gw_insric_state ) {
	case IS_IDLE:
		if( event == EV_SRIC_RX ) {
			/* Transmit the frame to the host */
			memcpy( hostser_txbuf, sric_rxbuf, sric_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );

			hostser_tx();

			gw_insric_state = IS_TRANSMITTING;
		}
		break;
	case IS_TRANSMITTING:
		if( event == EV_HOST_TX_COMPLETE )
			gw_insric_state = IS_IDLE;
		break;
	}
}

void sric_gw_hostser_rx( void )
{
	gw_inhost_fsm( EV_HOST_RX );
}

void sric_gw_hostser_tx_done( void )
{
	gw_insric_fsm( EV_HOST_TX_COMPLETE );
}

void sric_gw_sric_promisc_rx( const sric_if_t *iface )
{
	gw_insric_fsm( EV_SRIC_RX );
}

void sric_gw_sric_rx_resp( const sric_if_t *iface )
{
	gw_inhost_fsm( EV_SRIC_TX_COMPLETE );
}
