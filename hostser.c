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
#include "hostser.h"
#include "crc16.h"
#include <io.h>
#include <sys/cdefs.h>

typedef enum {
	HS_RX_IDLE,		/* Idle or receiving frame in 1 buffer */
	HS_RX_HAVE_FRAME,	/* Frame in 1 buffer, maybe receiving into 2nd*/
	HS_RX_FULL		/* Both buffers are full */
} hs_rx_state_t;

typedef enum {
	HS_TX_IDLE,		/* Nothing happening, capt'n */
	HS_TX_SENDING,		/* Transmitting data from one buffer */
} hs_tx_state_t;

typedef enum {
	EV_RX_RXED_FRAME,
	EV_RX_HANDLED_FRAME
} hs_rx_event_t;

typedef enum {
	EV_TX_TXMIT_DONE,
	EV_TX_QUEUED
} hs_tx_event_t;

static hs_rx_state_t rx_state = HS_RX_IDLE;
static hs_tx_state_t tx_state = HS_TX_IDLE;

/* Linked in elsewhere */
extern const hostser_conf_t hostser_conf;

/*** Transmit buffer ***/
static uint8_t txbuf[2][HOSTSER_BUF_SIZE];
static uint8_t txbuf_idx = 0;
uint8_t *hostser_txbuf = &txbuf[1][0];
uint8_t hostser_txlen = 0;

/* Offset of next byte to be transmitted from the tx buffer */
static uint8_t txbuf_pos = 0;

/**** Receive buffer ****/
static uint8_t rxbuf[2][HOSTSER_BUF_SIZE];
static uint8_t rxbuf_idx = 0;
uint8_t *hostser_rxbuf = &rxbuf[0][0];
/* Where the next byte needs to go */
static uint8_t rxbuf_pos = 0;

/* Set crc in transmit buffer */
static void tx_set_crc( void );

void hostser_init( void )
{
}

static void rx_fsm ( hs_rx_event_t ev )
{

	switch ( rx_state ) {
	case HS_RX_IDLE:
		if ( ev == EV_RX_RXED_FRAME ) {
			/* Point ptr for outside world to current buffer */
			hostser_rxbuf = &rxbuf[rxbuf_idx][0];
			/* Switch recieve destination to other buffer */
			rxbuf_idx = (rxbuf_idx + 1) & 1;
			rxbuf_pos = 0;
			/* And change state */
			rx_state = HS_RX_HAVE_FRAME;
		}
		break;

	case HS_RX_HAVE_FRAME:
		if ( ev == EV_RX_RXED_FRAME ) {
			/* Yikes. Host software should still be processing the
			 * first frame, so don't mess with hostser_rxbuf.
			 * instead sit tight in the RX_FULL state - this blocks
			 * any more receipt of data */
			rx_state = HS_RX_FULL;
		} else if ( ev == EV_RX_HANDLED_FRAME ) {
			/* Data is being read (or will be read) into the other
			 * rx buffer - no config change needed */
			rx_state = HS_RX_HAVE_FRAME;
		}
		break;

	case HS_RX_FULL:
		if ( ev == EV_RX_HANDLED_FRAME ) {
			/* Right - point host software at most recently received
			 * frame */
			hostser_rxbuf = &rxbuf[rxbuf_idx][0];
			/* And we can start reading into the other buffer */
			rxbuf_idx = (rxbuf_idx + 1) & 1;
			rxbuf_pos = 0;
			rx_state = HS_RX_HAVE_FRAME;
		}
		break;
	}

	return;
}

static void tx_fsm ( hs_tx_event_t ev )
{

	switch ( tx_state ) {
	case HS_TX_IDLE:
		if ( ev == EV_TX_QUEUED ) {
			/* Reset transmit position */
			txbuf_pos = 0;
			/* Swap buffers, transmit */
			hostser_txbuf = &txbuf[txbuf_idx][0];
			txbuf_idx = (txbuf_idx + 1) & 1;

			hostser_conf.usart_tx_start(
					hostser_conf.usart_tx_start_n);
		}
		break;

	case HS_TX_SENDING:
		if ( ev == EV_TXMIT_DONE ) {
			/* No change to buffer config required */
			if( hostser_conf.tx_done_cb != NULL )
				hostser_conf.tx_done_cb();
		} else if ( ev == EV_TX_QUEUED ) {
			/* For now, don't permit this. We'll block elsewhere
			 * until we're back in a state where we can xmit */
			while (1) ;
		}
		break;
	}

	return;
}

bool hostser_tx_cb( uint8_t *b )
{
	static bool escape_next = false;

	if( txbuf_pos == hostser_txlen ) {
		/* Transmission complete */
		tx_fsm( EV_TXMIT_DONE );
		return false;
	}

	*b = txbuf[txbuf_idx][txbuf_pos];

	if( escape_next ) {
		*b ^= 0x20;
		escape_next = false;

	/* Don't escape byte 0 (0x7E) */
	} else if( txbuf_pos != 0 && (*b == 0x7E || *b == 0x7D ) ) {
		*b = 0x7D;
		escape_next = true;
		return true;
	}

	txbuf_pos++;
	return true;
}

#define is_delim(x) ( (x == 0x7e) || (x == 0x8e) )

void hostser_rx_cb( uint8_t b )
{
	static bool escape_next = false;
	uint8_t len;
	uint16_t crc, recv_crc;

	if( is_delim(b) ) {
		escape_next = false;
		rxbuf_pos = 0;
	} else if( b == 0x7D ) {
		escape_next = true;
		return;
	} else if( escape_next ) {
		escape_next = false;
		b ^= 0x20;
	}
			
	/* End of buffer :/ */
	if( rxbuf_pos >= HOSTSER_BUF_SIZE )
		return;

	rxbuf[rxbuf_idx][rxbuf_pos] = b;
	rxbuf_pos += 1;

	if( !is_delim( rxbuf[rxbuf_idx][0] )
	    /* Make sure we've reached the minimum frame size */
	    || rxbuf_pos < (SRIC_LEN + 2) )
		return;

	len = rxbuf[rxbuf_idx][SRIC_LEN];
	if( len != rxbuf_pos - (SRIC_LEN + 3) )
		return;

	/* Everything gets hashed */
	crc = crc16( rxbuf[rxbuf_idx], rxbuf_pos - 2 );

	recv_crc = rxbuf[rxbuf_idx][ rxbuf_pos-2 ];
	recv_crc |= rxbuf[rxbuf_idx][ rxbuf_pos-1 ] << 8;

	if( crc == recv_crc ) {

		/* Swap buffers over - have to do this before callback because
		 * it'll call hostser_rx_done. In a situation where the cb
		 * didn't occur in interrupt context, this wouldn't have to
		 * happen: comming soon! */

		/* Receive into the /other/ buffer */
		rxbuf_idx = (rxbuf_idx + 1) & 1;

		/* We have a valid frame :-O */
		if( hostser_conf.rx_cb != NULL )
			hostser_conf.rx_cb();
	}
}

static void tx_set_crc( void )
{
	uint8_t len = hostser_txbuf[SRIC_LEN];
	uint16_t c = crc16( hostser_txbuf, SRIC_HEADER_SIZE + len );

	hostser_txbuf[ SRIC_DATA + len ] = c & 0xff;
	hostser_txbuf[ SRIC_DATA + len + 1 ] = (c >> 8) & 0xff;
}

void hostser_rx_done( void )
{
	/* Change outside view of what's in the receive buffer */
	hostser_rxbuf = &rxbuf[rxbuf_idx][0];
}

bool hostser_tx_busy( void )
{
	return tx_state == HS_TX_SENDING;
}

void hostser_tx( void )
{

	/* We can't start transmitting until we've stopped transmitting.
	 * Clearly this is going to spin forever if we call hostser_tx in
	 * interrupt context and something's in progress */
	while( hostser_tx_busy() )
		;

	tx_set_crc();
	hostser_txlen = SRIC_OVERHEAD + hostser_txbuf[ SRIC_LEN ];

	tx_fsm( EV_TX_QUEUE );
}
