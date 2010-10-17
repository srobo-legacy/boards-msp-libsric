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
#include <drivers/sched.h>
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
	/* Start bus initialisation */
	EV_SRIC_INIT,
	/* Timeout */
	EV_TIMEOUT,
} gw_event_t;

/* State machine states */
typedef enum {
	/* Idle */
	S_IDLE,
	/* Transmitting the command the host sent us over sric */
	S_SRIC_TX_CMD,
	/* Transmitting the sric response to the host */
	S_HOST_TX_RESP,
} state_t;

static void gw_fsm( gw_event_t event );

static bool gw_timeout_handler( void *ud )
{
	gw_fsm( EV_TIMEOUT );
	return false;
}

static state_t gw_state;

void sric_gw_init( void )
{
	gw_state = S_IDLE;
}

/* State machine for this gateway */
static void gw_fsm( gw_event_t event )
{
	switch( gw_state ) {
	case S_IDLE:
		/* Idling away, plotting our revenge */
		if( event == EV_HOST_RX ) {
			/* Transmit frame on SRIC */
			memcpy( sric_txbuf, hostser_rxbuf, hostser_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );
			hostser_rx_done();

			sric_if.tx_lock();
			sric_if.tx_cmd_start( hostser_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE,
					      /* Expect responses to everything except broadcasts */
					      sric_txbuf[SRIC_DEST] != 0 );

			gw_state = S_SRIC_TX_CMD;
		}
		else if( event == EV_SRIC_RX ) {
			/* TODO -- Process commands from SRIC side */
		}
		break;

	case S_SRIC_TX_CMD:
		/* Waiting for the SRIC client to get back to us */
		if( event == EV_SRIC_RX ) {
			/* Response received on SRIC */
			/* Transmit it to the host */
			memcpy( hostser_txbuf, sric_rxbuf, sric_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );

			hostser_tx();
			gw_state = S_HOST_TX_RESP;
		} else if( event == EV_SRIC_ERROR ) {
			/* SRIC interface has given up waiting for the slave */
			/* TODO: Perhaps notify the host of the error */
			gw_state = S_IDLE;
		}

	case S_HOST_TX_RESP:
		/* Transmitting the SRIC response  */
		if( event == EV_HOST_TX_COMPLETE ) {
			/* We're done with everything */
			hostser_rx_done();
			gw_state = S_IDLE;
		}

	default:
		break;
	}
}

void sric_gw_hostser_rx( void )
{
	gw_fsm( EV_HOST_RX );
}

void sric_gw_hostser_tx_done( void )
{
	gw_fsm( EV_HOST_TX_COMPLETE );
}

uint8_t sric_gw_sric_rxcmd( const sric_if_t *iface )
{
	gw_fsm( EV_SRIC_RX );

	/* We'll provide our response later */
	return SRIC_RESPONSE_DEFER;
}

void sric_gw_sric_rxresp( const sric_if_t *iface )
{
	gw_fsm( EV_SRIC_RX );
}

void sric_gw_sric_err( void )
{
	gw_fsm( EV_SRIC_ERROR );
}

static bool gw_timeout_cb( void *udata )
{
	gw_fsm ( EV_TIMEOUT );
	return false;
}

void sric_gw_init_bus( void )
{
	gw_fsm( EV_SRIC_INIT );
}
