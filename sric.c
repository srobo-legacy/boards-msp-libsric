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
#include "sric.h"
#include <io.h>
#include <sys/cdefs.h>
#include "crc16.h"
#include <drivers/sched.h>

/* One additional byte for the 0x7e for correct stop bit receivage */
uint8_t sric_txbuf[SRIC_TXBUF_SIZE+1];
uint8_t sric_txlen;

/* Number of token loops to retransmit after */
#define TOKEN_THRESHOLD 3
/* Number of times the token's been seen this loop */
static uint8_t token_count;

static struct {
	/* Next byte to be transmitted */
	uint8_t out_pos;
} tx;

uint8_t sric_rxbuf[SRIC_RXBUF_SIZE];
static uint8_t rxbuf_pos;

extern const sric_conf_t sric_conf;
uint8_t sric_addr;

static sched_task_t timeout_task;

/* Events that trigger state changes */
typedef enum {
	/* Request for a lock on the transmit buffer */
	EV_TX_LOCK,
	/* Request to transmit the contents of the transmit buffer */
	EV_TX_START,
	/* Transmission's complete */
	EV_TX_DONE,
	/* Frame received */
	EV_RX,
	/* Timeout waiting for response */
	EV_TIMEOUT,
	/* Got token */
	EV_GOT_TOKEN,
} event_t;

/* States */
static volatile enum {
	/* Not much going on */
	S_IDLE,
	/* Waiting for our response to be assembled */
	S_WAIT_ASM_RESP,
	/* Transmit buffer's locked and being filled */
	S_TX_LOCKED,
	/* Waiting for the token to transmit command */
	S_TX_WAIT_TOKEN,
	/* Transmitting */
	S_TX,
	/* Transmitting, but timeout occurred.
	   Waiting for tx buffer to run dry before returning to S_IDLE */
	S_TX_TIMED_OUT,
	/* Waiting for response */
	S_WAIT_RESP,
	/* Waiting for token to transmit response */
	S_TX_RESP_WAIT_TOKEN,
	/* Transmitting response */
	S_TX_RESP,
} state;

static void sric_tx_lock( void );
static void sric_tx_start( uint8_t len );

const sric_if_t sric_if = {
	.txbuf = sric_txbuf,
	.rxbuf = sric_rxbuf,
	.tx_lock = sric_tx_lock,
	.tx_cmd_start = sric_tx_start,
};

static void fsm( event_t ev );

#define lvds_tx_en() do { (*sric_conf.txen_port) |= sric_conf.txen_mask; } while(0)
#define lvds_tx_dis() do { (*sric_conf.txen_port) &= ~sric_conf.txen_mask; } while(0)

void sric_init( void )
{
	if( SRIC_DIRECTOR ) {
		sric_addr = 1;
	} else {
		sric_addr = 3;
	}

	lvds_tx_dis();
	(*sric_conf.txen_dir) |= sric_conf.txen_mask;
}

/* Set the CRC in the transmit buffer */
static void crc_txbuf( void )
{
	uint8_t len = sric_txbuf[ SRIC_LEN ];
	uint16_t c = crc16( sric_txbuf, SRIC_HEADER_SIZE + len );

	sric_txbuf[ SRIC_DATA + len ] = c & 0xff;
	sric_txbuf[ SRIC_DATA + len + 1 ] = (c >> 8) & 0xff;
}

static void start_tx( void )
{
	/* Add an additional 0x7e on the end of the frame
	   to allow stop bits to be received correctly */
	sric_txbuf[ sric_txlen ] = 0x7e;
	sric_txlen++;

	sric_conf.usart_rx_gate(sric_conf.usart_n, false);
	lvds_tx_en();
	tx.out_pos = 0;
	sric_conf.usart_tx_start(sric_conf.usart_n);
}

static bool timeout( void *ud )
{
	fsm( EV_TIMEOUT );
	return false;
}

static void register_timeout( void )
{
	/* Setup a long timeout for the response */
	timeout_task.t = 15000;
	timeout_task.cb = timeout;
	sched_add(&timeout_task);
}

static void fsm( event_t ev )
{
	switch(state) {
	case S_IDLE:
		if(ev == EV_TX_LOCK) {
			/* Disable the receiver */
			sric_conf.usart_rx_gate(sric_conf.usart_n, false);

			state = S_TX_LOCKED;
		} else if(ev == EV_RX) {
			/* Received a frame */
			uint8_t l = sric_conf.rx_cmd(&sric_if);

			if( l == SRIC_RESPONSE_DEFER ) {
				/* Response isn't ready yet.  Wait. */
				state = S_WAIT_ASM_RESP;
			} else if( l <= MAX_PAYLOAD ) {
				crc_txbuf();
				sric_txlen = l + 2;

				sric_conf.token_drv->req();
				state = S_TX_RESP_WAIT_TOKEN;
			}
		}
		break;

	case S_WAIT_ASM_RESP:
		/* TODO! */
		while(1);
		/* sric_conf.token_drv->req(); */
		break;

	case S_TX_LOCKED:
		/* Transmit buffer's locked */
		if(ev == EV_TX_START) {
			/* Generate the checksum */
			crc_txbuf();
			sric_txlen += 2;

			sric_conf.token_drv->req();
			state = S_TX_WAIT_TOKEN;
		}
		break;

	case S_TX_WAIT_TOKEN:
		if( ev == EV_GOT_TOKEN ) {
			register_timeout();
			token_count = 0;
			start_tx();
			state = S_TX;
		}
		break;

	case S_TX:
		/* Transmitting a frame */
		if(ev == EV_TX_DONE) {
			lvds_tx_dis();
			sric_conf.usart_rx_gate(sric_conf.usart_n, true);
			sric_conf.token_drv->release();

			/* Re-request the token for retransmission */
			sric_conf.token_drv->req();

			state = S_WAIT_RESP;
		} else if( ev == EV_TIMEOUT ) {
			/* Timeout occured whilst transmitting */
			/* Need to mop up the events cleanly,
			   so go via the S_TX_TIMED_OUT state to wait for the tx to finish. */

			/* Don't let anyone hear the rest of our transmission */
			lvds_tx_dis();

			state = S_TX_TIMED_OUT;
		}
		break;

	case S_TX_TIMED_OUT:
		/* Finished transmitting */
		if(ev == EV_TX_DONE) {
			sric_conf.usart_rx_gate(sric_conf.usart_n, true);
			sric_conf.token_drv->release();

			/* Emit the error callback */
			if( sric_conf.error != NULL )
				sric_conf.error();

			state = S_IDLE;
		}
		break;

	case S_WAIT_RESP:
		/* Waiting for a response */
		if(ev == EV_RX) {
			/* Cancel the timeout */
			sched_rem(&timeout_task);
			/* No longer need the token for retransmission */
			sric_conf.token_drv->cancel_req();

			if( sric_conf.rx_resp != NULL )
				sric_conf.rx_resp( &sric_if );

			state = S_IDLE;
		} else if( ev == EV_TIMEOUT ) {
			/* Drop our token request */
			sric_conf.token_drv->cancel_req();

			/* Emit the error callback */
			if( sric_conf.error != NULL )
				sric_conf.error();

			state = S_IDLE;
		} else if( ev == EV_GOT_TOKEN ) {
			token_count++;

			if(token_count >= TOKEN_THRESHOLD) {
				token_count = 0;
				start_tx();
				state = S_TX;
			} else {
				sric_conf.token_drv->release();
				sric_conf.token_drv->req();
			}
		}
		break;

	case S_TX_RESP_WAIT_TOKEN:
		if( ev == EV_GOT_TOKEN ) {
			start_tx();
			state = S_TX_RESP;
		}
		break;

	case S_TX_RESP:
		/* Transmitting response frame */
		if(ev == EV_TX_DONE ) {
			lvds_tx_dis();
			sric_conf.usart_rx_gate(sric_conf.usart_n, true);
			sric_conf.token_drv->release();
			state = S_IDLE;
		}
		break;

	default:
		state = S_IDLE;
	}
}


bool sric_tx_cb( uint8_t *b )
{
	static bool escape_next = false;

	if( tx.out_pos == sric_txlen ) {
		/* Transmission complete */
		fsm( EV_TX_DONE );
		return false;
	}

	*b = sric_txbuf[tx.out_pos];

	if( escape_next ) {
		*b ^= 0x20;
		escape_next = false;

	/* Don't escape byte 0 (0x7E) */
	} else if( tx.out_pos != 0 && (*b == 0x7E || *b == 0x7D ) ) {
		*b = 0x7D;
		escape_next = true;
		return true;
	}

	tx.out_pos++;
	return true;
}

void sric_rx_cb( uint8_t b )
{
	static bool escape_next = false;
	uint8_t len;
	uint16_t crc, recv_crc;

	if( b == 0x7E ) {
		escape_next = false;
		rxbuf_pos = 0;
	} else if( b == 0x7D ) {
		escape_next = true;
		return;
	} else if( escape_next ) {
		escape_next = false;
		b ^= 0x20;
	}

	/* End of buffer */
	if( rxbuf_pos >= SRIC_RXBUF_SIZE )
		return;

	sric_rxbuf[rxbuf_pos] = b;
	rxbuf_pos += 1;

	if( sric_rxbuf[0] != 0x7e
	    /* Make sure we've reached the minimum frame size */
	    || rxbuf_pos < (SRIC_LEN + 2) )
		return;

	len = sric_rxbuf[SRIC_LEN];
	if( len != rxbuf_pos - (SRIC_LEN + 3) )
		return;

	/* Everything gets hashed */
	crc = crc16( sric_rxbuf, rxbuf_pos - 2 );

	recv_crc = sric_rxbuf[ rxbuf_pos-2 ];
	recv_crc |= sric_rxbuf[ rxbuf_pos-1 ] << 8;

	if( crc == recv_crc )
		/* We have a valid frame :-O */
		fsm( EV_RX );

	rxbuf_pos = 0;
}

static void sric_tx_lock( void )
{
	while( state != S_TX_LOCKED )
		fsm(EV_TX_LOCK);
}

static void sric_tx_start( uint8_t len )
{
	sric_txlen = len;
	fsm(EV_TX_START);
}

void sric_haz_token( void )
{
	fsm(EV_GOT_TOKEN);
}
