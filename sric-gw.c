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
	/* Uninitialised state */
	S_UNINIT,
	/* Sending out reset commands to the bus */
	S_RESET_BUS,
	/* Pausing waiting for the token to propagate out */
	S_TOK_PAUSE,
	/* Sending a node its address */
	S_SEND_ADDRESS,
	/* Waiting for client info to be sent to host */
	S_WAIT_INFO_TO_HOST,
	/* Waiting for the token to be advanced */
	S_WAIT_ADVANCE_ACK,
	/* Waiting for the token or timeout */
	S_WAIT_TOK,
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
	if( SRIC_DIRECTOR ) {
		gw_state = S_UNINIT;
	} else {
		gw_state = S_IDLE;
	}
}

#if (SRIC_DIRECTOR)
/* Send RESET command on the SRIC bus */
static void send_reset( void )
{
	sric_if.tx_lock();

	sric_if.txbuf[0] = 0x7e;
	sric_if.txbuf[SRIC_DEST] = 0;
	sric_if.txbuf[SRIC_SRC] = sric_addr;
	sric_if.txbuf[SRIC_LEN] = 1;
	sric_if.txbuf[SRIC_DATA] = 0x80 | SRIC_SYSCMD_RESET;

	sric_if.tx_lock();
	sric_if.tx_cmd_start( sric_if.txbuf[SRIC_LEN] + SRIC_HEADER_SIZE );
}

/* Send ADDRESS_ASSIGN command on the SRIC bus */
static void send_address( uint8_t addr )
{
	sric_if.tx_lock();

	sric_if.txbuf[0] = 0x7e;
	sric_if.txbuf[SRIC_DEST] = 0;
	sric_if.txbuf[SRIC_SRC] = sric_addr;
	sric_if.txbuf[SRIC_LEN] = 2;
	sric_if.txbuf[SRIC_DATA] = 0x80 | SRIC_SYSCMD_ADDR_ASSIGN;
	sric_if.txbuf[SRIC_DATA+1] = addr;

	sric_if.tx_lock();
	sric_if.tx_cmd_start( sric_if.txbuf[SRIC_LEN] + SRIC_HEADER_SIZE );
}

/* Send ADVANCE_TOKEN command on the SRIC bus */
static void send_tok_advance( uint8_t addr )
{
	/* TODO: Populate SRIC frame with ADVANCE_TOKEN and transmit it */
}

static sched_task_t gw_task;
static bool gw_timeout_cb( void *udata );

static void gw_register_timeout( void )
{
	gw_task.t = 50;
	gw_task.cb = gw_timeout_cb;
	sched_add( &gw_task );
}
#endif

/* State machine for this gateway */
static void gw_fsm( gw_event_t event )
{
	switch( gw_state ) {
#if (SRIC_DIRECTOR)
		static uint8_t reset_count;
		static uint8_t assign_addr;

	case S_UNINIT:
		/* Bus not yet initialised */
		if( event == EV_SRIC_INIT ) {
			sric_if.use_token( false );
			reset_count = 0;
			assign_addr = 5;

			send_reset();
			gw_state = S_RESET_BUS;
		}
		break;

	case S_RESET_BUS:
		/* Broadcast transmission's complete */
		if( event == EV_SRIC_RX ) {
			reset_count++;
			if( reset_count == 10 ) {
				token_dir_emit_first();
				gw_register_timeout();

				gw_state = S_TOK_PAUSE;
			} else {
				gw_register_timeout();
			}
		} else if( event == EV_TIMEOUT ) {
			send_reset();
		}
		break;

	case S_TOK_PAUSE:
		if( event == EV_TIMEOUT ) {
			send_address(assign_addr);

			gw_state = S_SEND_ADDRESS;
		}
		break;

	case S_SEND_ADDRESS:
		if ( event == EV_SRIC_RX ) {
			/* TODO: Tell the host about the situation */
			/* TODO: gw_state = S_WAIT_INFO_TO_HOST; */

			token_dir_drv.req();
			send_tok_advance(assign_addr);

			/* Move straight to S_WAIT_ADVANCE_ACK for now -- transmit to host soon! */
			gw_state = S_WAIT_ADVANCE_ACK;
		}
		break;

	case S_WAIT_INFO_TO_HOST:
		break;

	case S_WAIT_ADVANCE_ACK:
		if( event == EV_SRIC_RX ) {
			gw_register_timeout();
			gw_state = S_WAIT_TOK;
		}
		break;

	case S_WAIT_TOK:
		if( event == EV_TIMEOUT ) {
			if( sric_conf.token_drv->have_token() ) {
				/* We've finished enumerating the bus */
				/* TODO: Tell the host we're done */
				sric_if.ctl( SRIC_CTL_RELEASE_TOK );
				sric_if.use_token(true);
				gw_state = S_IDLE;
				break;
			}

			/* Send the next guy his address */
			assign_addr++;
			if( assign_addr == 127 )
				while(1);
			send_address(assign_addr);

			gw_state = S_SEND_ADDRESS;
		}
		break;
#endif

	case S_IDLE:
		/* Idling away, plotting our revenge */
		if( event == EV_HOST_RX ) {
			/* Transmit frame on SRIC */
			memcpy( sric_txbuf, hostser_rxbuf, hostser_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE );
			hostser_rx_done();

			sric_if.tx_lock();
			sric_if.tx_cmd_start(hostser_rxbuf[SRIC_LEN] + SRIC_HEADER_SIZE);
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
