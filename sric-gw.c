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
	IH_TRANSMITTING_SRIC
} inhost_state_t;

typedef enum {
	IS_IDLE,
	IS_TRANSMITTING,
	IS_FULL
} insric_state_t;

static inhost_state_t gw_inhost_state;
static insric_state_t gw_insric_state;

static void gw_insric_fsm( gw_event_t event );
static void gw_inhost_fsm( gw_event_t event );
static void gw_sric_if_ctl( sric_ctl_t c );
static void gw_sric_if_use_token( bool b );
static void gw_sric_if_tx_lock( void );

static sric_if_t gw_sric_if = {
	.ctl = gw_sric_if_ctl,
	.use_token = gw_sric_if_use_token,
	.tx_lock = gw_sric_if_tx_lock
};

void sric_gw_init( void )
{

	gw_sric_if.txbuf = hostser_txbuf;
}

static void gw_sric_if_ctl ( sric_ctl_t c )
{

	return;
}

static void gw_sric_if_use_token ( bool b )
{

	return;
}

static void gw_sric_if_tx_lock( void )
{

	return;
}


static bool gw_fwd_to_sric()
{
	int ret;

	/* Retain frame for SRIC transmission */
	memcpy( sric_txbuf, gw_sric_if.rxbuf,
			gw_sric_if.rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );

	/* Always place msg on SRIC */
	sric_if.tx_lock();
	sric_if.tx_cmd_start( gw_sric_if.rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE,
		      /* Avoid SRIC IF rotating by not expecting a response */
			      false );

	/* Hand to power/pc-sric board client code - unless there's no space
	 * for any response it might generate to be stored */
	if( gw_insric_state == IS_FULL ) {
		return false;
	}

	ret = sric_conf.rx_cmd( &gw_sric_if );

	if ((ret & SRIC_LENGTH_MASK) <= (MAX_FRAME_LEN-2)) {
		/* Hello - gateway device has a response */
		gw_insric_fsm( EV_SRIC_RX );
	}

	return true;
}

#define require_len(x) do { if( gw_sric_if.rxbuf[SRIC_LEN] != x ) return false; } while(0)

static bool gw_proc_host_cmd()
{
	uint8_t len = gw_sric_if.rxbuf[SRIC_LEN];
	uint8_t *data = gw_sric_if.rxbuf + SRIC_DATA;

	if( len == 0 ) {
		return false;
	}

	if( gw_insric_state == IS_FULL ) {
		return false;
	}

	gw_sric_if.txbuf[0] = 0x8e;
	gw_sric_if.txbuf[SRIC_DEST] = 1 | 0x80; /* Destination is bus director*/
					/* Additionally, this is an ack */
	gw_sric_if.txbuf[SRIC_SRC] = 0;	/* XXX - what's an appropriate addr
					 * for the gateway to send msgs from? */
	gw_sric_if.txbuf[SRIC_LEN] = 0;

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
		gw_sric_if.txbuf[SRIC_LEN] = 1;
		gw_sric_if.txbuf[SRIC_DATA] = sric_conf.token_drv->have_token();
		break;

#if SRIC_DIRECTOR
	case GW_CMD_GEN_TOKEN:
		require_len(1);

		/* If we have the token, release it; otherwise generate it */
		if (sric_conf.token_drv->have_token()) {
			sric_conf.token_drv->release();
		} else {
			token_dir_emit_first();
		}
		break;
#endif
	}

	/* Calling insric FSM from within inhost FSM: should be fine, there are
	 * no paths from insric FSM to inhost. And being full duplex, the host
	 * interface state doesn't (shouldn't) share any state */
	gw_insric_fsm( EV_SRIC_RX );
	return true;
}

/* Manages data coming in from the host */
static void gw_inhost_fsm( gw_event_t event )
{
	gw_sric_if.rxbuf = hostser_rxbuf;

	if( event == EV_SRIC_TX_COMPLETE ) {
		gw_inhost_state = IH_IDLE;
		return;
	} else if ( event == EV_HOST_RX ) {
		inhost_state_t new = IH_IDLE;
		bool (*f)(void) = NULL;

		if( hostser_rxbuf[0] == 0x7e &&
		    gw_inhost_state != IH_TRANSMITTING_SRIC) {
			f = gw_fwd_to_sric;
			new = IH_TRANSMITTING_SRIC;
		} else if( hostser_rxbuf[0] == 0x8e ) {
			f = gw_proc_host_cmd;
			new = gw_inhost_state;
		}

		if( f != NULL && f() )
			gw_inhost_state = new;

		hostser_rx_done();
	}
}

/* Manages data coming in from the sric bus */
static void gw_insric_fsm( gw_event_t event )
{
	switch( gw_insric_state ) {
	case IS_IDLE:
		if( event == EV_SRIC_RX ) {
			/* Transmit the frame to the host */

			hostser_tx();
			gw_insric_state = IS_TRANSMITTING;

			/* Swap over buffers */
			gw_sric_if.txbuf = hostser_txbuf;
		}
		break;

	case IS_TRANSMITTING:
		if( event == EV_HOST_TX_COMPLETE ) {
			/* Buffer freed */
			gw_insric_state = IS_IDLE;
		} else if ( event == EV_SRIC_RX ) {
			/* Another frame */
			hostser_tx();
			gw_insric_state = IS_FULL;
			gw_sric_if.txbuf = hostser_txbuf;
		}
		break;

	case IS_FULL:
		if ( event == EV_HOST_TX_COMPLETE ) {
			gw_insric_state = IS_TRANSMITTING;
		}
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

	if( gw_insric_state == IS_FULL ) {
		/* No space -> don't transmit */
		return;
	}

	memcpy( gw_sric_if.txbuf, iface->rxbuf, iface->rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );
	gw_insric_fsm( EV_SRIC_RX );
}

void sric_gw_sric_rx_resp( const sric_if_t *iface )
{
	gw_inhost_fsm( EV_SRIC_TX_COMPLETE );
}
