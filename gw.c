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
#include "gw.h"

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

static state_t gw_state = S_IDLE;

void gw_init( void )
{
}

/* State machine for this gateway */
static void gw_fsm( gw_event_t event )
{
	switch( gw_state ) {
	case S_IDLE:
		/* Idling away, plotting our revenge */
		if( event == EV_HOST_RX ) {
			uint8_t i;
			/* Transmit frame on SRIC */
			for( i=0;
			     i<(hostser_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE);
			     i++ )
				sric_txbuf[i] = hostser_rxbuf[i];

			sric_tx();
			gw_state = S_SRIC_TX_CMD;
		}
		else if( event == EV_SRIC_RX ) {
			/* TODO -- Process commands from SRIC side */
		}
		break;

	case S_SRIC_TX_CMD:
		/* Waiting for the SRIC client to get back to us */
		if( event == EV_SRIC_RX ) {
			uint8_t i;
			/* Response received on SRIC */
			/* Transmit it to the host */
			for( i=0;
			     i<(sric_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE);
			     i++ )
				hostser_txbuf[i] = sric_rxbuf[i];

			hostser_tx();
			gw_state = S_HOST_TX_RESP;
		}

	case S_HOST_TX_RESP:
		/* Transmitting the SRIC response  */
		if( event == EV_HOST_TX_COMPLETE ) {
			/* We're done with everything */
			hostser_rx_done();
			sric_rx_done();
			gw_state = S_IDLE;
		}

	default:
		break;
	}
}

void gw_hostser_rx( void )
{
	gw_fsm( EV_HOST_RX );
}

void gw_hostser_tx_done( void )
{
	gw_fsm( EV_HOST_TX_COMPLETE );
}

void gw_sric_rx( void )
{
	gw_fsm( EV_SRIC_RX );
}

