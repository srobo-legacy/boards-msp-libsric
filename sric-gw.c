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

#include <drivers/sched.h>

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

typedef enum {
	DEV_IDLE,
	DEV_WAITING
} gwdev_state_t;

static inhost_state_t gw_inhost_state;
static insric_state_t gw_insric_state;
static gwdev_state_t gw_dev_state;
static volatile bool gw_dev_timed_out;
static uint8_t *gw_dev_frame_ptr;

static void gw_insric_fsm( gw_event_t event );
static void gw_inhost_fsm( gw_event_t event );
static void gw_sric_if_ctl( sric_ctl_t c );
static void gw_sric_if_use_token( bool b );
static void gw_sric_if_tx_lock( void );
static void gw_sric_tx_cmd_start( uint8_t len, bool expect_resp );
static bool gw_dev_timeout( void *dummy );

sric_if_t gw_sric_if = {
	.ctl = gw_sric_if_ctl,
	.use_token = gw_sric_if_use_token,
	.tx_lock = gw_sric_if_tx_lock,
	.tx_cmd_start = gw_sric_tx_cmd_start
};

const static sched_task_t gw_dev_retransmit = {
	.t = 10,		/* Suggestions are welcome */
	.cb = gw_dev_timeout
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

static bool gw_dev_timeout( void *dummy )
{

	gw_dev_timed_out = true;
	return false;
}

static void gw_sric_if_tx_lock( void )
{

	/* While we have no free buffers or we're busy retransmitting, spin */
	while ( gw_dev_state == DEV_WAITING || gw_insric_state == IS_FULL ) {
		hostser_poll();		/* For us to free a buffer */
		sric_gw_poll();		/* Permit retransmission */
		sric_poll();		/* Allow rest of sric to operate :| */
	}

	return;
}

static void gw_sric_tx_cmd_start( uint8_t len, bool expect_resp )
{

	if( gw_insric_state == IS_FULL && !expect_resp ) {
		/* No space -> no joy */
		return;
	}

	gw_dev_frame_ptr = gw_sric_if.txbuf;
	gw_insric_fsm( EV_SRIC_RX );

	if ( expect_resp ) {
		gw_dev_state = DEV_WAITING;
		gw_dev_timed_out = false;
		sched_add( &gw_dev_retransmit );
	}

	return;
}

void sric_gw_poll()
{

	if ( gw_dev_timed_out ) {
		if ( gw_insric_state == IS_FULL ) {
			/* Can't retransmitt */
			return;
		}

		if ( hostser_txbuf != gw_dev_frame_ptr )
			memcpy( hostser_txbuf, gw_dev_frame_ptr, 70 );

		hostser_tx();

		gw_dev_timed_out = false;

		sched_rem( &gw_dev_retransmit );
		sched_add( &gw_dev_retransmit );
	}
}

static bool gw_proc_bus_cmd()
{
	int ret;

	/* Is this destined for the gateway device, the bus, or both? */
	if ( ( gw_sric_if.rxbuf[SRIC_DEST] & 0x7F ) != sric_addr ||
				gw_sric_if.rxbuf[SRIC_DEST] == 0 ) {

		/* not for local dev, or broadcast; put on bus. */
		memcpy( sric_txbuf, gw_sric_if.rxbuf,
			gw_sric_if.rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );

		sric_if.tx_lock();

		/* Avoid SRIC IF rotating by not expecting a response */
		sric_if.tx_cmd_start(
			gw_sric_if.rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE, false );

		/* Update state to reflect the fact we just put something on
		 * the bus */
		gw_inhost_state = IH_TRANSMITTING_SRIC;
	}

	/* If it's for this device: */
	if ( ( gw_sric_if.rxbuf[SRIC_DEST] & 0x7F ) == sric_addr ||
				gw_sric_if.rxbuf[SRIC_DEST] == 0 ) {
		/* An ack? */
		if ( gw_sric_if.rxbuf[SRIC_DEST] & 0x80 ) {
			if ( gw_dev_state == DEV_WAITING ) {
				sched_rem( &gw_dev_retransmit );
				gw_dev_timed_out = false;
				gw_dev_state = DEV_IDLE;
			}

			/* XXX: passing ack data to local device? */
		} else {

			/* Normal req. Discard if we can't store a response */
			if( gw_insric_state == IS_FULL ) {
				return false;
			}

			ret = sric_conf.rx_cmd( &gw_sric_if );

			if ((ret & SRIC_LENGTH_MASK) <= (MAX_FRAME_LEN-2)) {
				/* Hello - gateway device has a response */
				gw_insric_fsm( EV_SRIC_RX );
			}
		}
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
		bool (*f)(void) = NULL;

		if( hostser_rxbuf[0] == 0x7e &&
		    gw_inhost_state != IH_TRANSMITTING_SRIC) {
			f = gw_proc_bus_cmd;
		} else if( hostser_rxbuf[0] == 0x8e ) {
			f = gw_proc_host_cmd;
		}

		/* NOTE: gw_proc_bus_cmd may change gw_inhost_state to
		 * IH_TRANSMITTING_SRIC, depending on whether it actually puts
		 * this frame on SRIC */
		if( f != NULL)
			f();

		hostser_rx_done();
	}
}

/* Manages data coming in from the sric bus */
static void gw_insric_fsm( gw_event_t event )
{

	if ( event == EV_SRIC_RX && gw_dev_state == DEV_WAITING)
		/* Don't accept new frames while we're (possibly)
		 * retransmitting one from this device */
		/* This could be heavily improved */
		return;

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
