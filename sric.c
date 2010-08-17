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
#include "crc16.h"

uint8_t sric_txbuf[SRIC_TXBUF_SIZE];
uint8_t sric_txlen;

static struct {
	/* Next byte to be transmitted */
	uint8_t out_pos;
} tx;

uint8_t sric_rxbuf[SRIC_RXBUF_SIZE];
static uint8_t rxbuf_pos;

extern const sric_conf_t sric_conf;
uint8_t sric_addr;

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
} event_t;

/* States */
static volatile enum {
	/* Not much going on */
	S_IDLE,
	/* Transmit buffer's locked and being filled */
	S_TX_LOCKED,
	/* Transmitting */
	S_TX,
	/* Waiting for response */
	S_WAIT_RESP,
	/* Transmitting response */
	S_TX_RESP,
} state;

static void fsm( event_t ev );

#define SRIC_TXEN (1<<0)
#define lvds_tx_en() do { P3OUT |= SRIC_TXEN; } while(0)
#define lvds_tx_dis() do { P3OUT &= ~SRIC_TXEN; } while(0)

void sric_init( void )
{
	if( SRIC_DIRECTOR ) {
		sric_addr = 1;
	} else {
		sric_addr = 0;
	}

	lvds_tx_dis();
	P3DIR |= SRIC_TXEN;
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
			/* TODO */

			/* Start transmitting response */
			lvds_tx_en();
			tx.out_pos = 0;
			sric_conf.usart_tx_start(sric_conf.usart_n);

			state = S_TX_RESP;
		}
		break;

	case S_TX_LOCKED:
		/* Transmit buffer's locked */
		if(ev == EV_TX_START) {
			/* Ready to start transmitting */
			lvds_tx_en();

			/* Start transmitting */
			tx.out_pos = 0;
			sric_conf.usart_tx_start(sric_conf.usart_n);

			state = S_TX;
		}
		break;

	case S_TX:
		/* Transmitting a frame */
		if(ev == EV_TX_DONE) {
			lvds_tx_dis();
			sric_conf.usart_rx_gate(sric_conf.usart_n, true);

			state = S_WAIT_RESP;
		}
		break;

	case S_WAIT_RESP:
		/* Waiting for a response */
		if(ev == EV_RX) {
			/* TODO: Call callback */
			state = S_IDLE;
		}
		break;

	case S_TX_RESP:
		/* Transmitting response frame */
		if(ev == EV_TX_DONE ) {
			lvds_tx_dis();
			sric_conf.usart_rx_gate(sric_conf.usart_n, true);

			state = S_IDLE;
		}

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

	if( crc == recv_crc ) {
		/* We have a valid frame :-O */
		fsm( EV_RX );
	} else
		/* CRC's invalid -- throw it away */
		rxbuf_pos = 0;

	nop();
}

void sric_tx( void )
{

}

void sric_tx_resp( void )
{

}

void sric_rx_done( void )
{

}
