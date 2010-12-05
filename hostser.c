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

/* Linked in elsewhere */
extern const hostser_conf_t hostser_conf;

/*** Transmit buffer ***/
static uint8_t txbuf[2][HOSTSER_BUF_SIZE];
static uint8_t txbuf_idx = 0;
uint8_t *hostser_txbuf = &txbuf[0][0];
uint8_t hostser_txlen = 0;

/* Offset of next byte to be transmitted from the tx buffer */
static uint8_t txbuf_pos = 0;

/**** Receive buffer ****/
static uint8_t rxbuf[2][HOSTSER_BUF_SIZE];
static uint8_t rxbuf_idx = 0;
uint8_t *hostser_rxbuf = &rxbuf[0][0];
/* Where the next byte needs to go */
static uint8_t rxbuf_pos = 0;

static bool rxed_frame = false;
static bool txing_frame = false;

/* Set crc in transmit buffer */
static void tx_set_crc( void );

void hostser_init( void )
{
}

bool hostser_tx_cb( uint8_t *b )
{
	static bool escape_next = false;

	if( txbuf_pos == hostser_txlen ) {
		/* Transmission complete */
		txing_frame = false;
		txbuf_idx = (txbuf_idx + 1) & 1;

		if( hostser_conf.tx_done_cb != NULL )
			hostser_conf.tx_done_cb();

		return false;
	}

	*b = hostser_txbuf[txbuf_pos];

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

	hostser_rxbuf[rxbuf_pos] = b;
	rxbuf_pos += 1;

	if( !is_delim( hostser_rxbuf[0] )
	    /* Make sure we've reached the minimum frame size */
	    || rxbuf_pos < (SRIC_LEN + 2) )
		return;

	len = hostser_rxbuf[SRIC_LEN];
	if( len != rxbuf_pos - (SRIC_LEN + 3) )
		return;

	/* Everything gets hashed */
	crc = crc16( hostser_rxbuf, rxbuf_pos - 2 );

	recv_crc = hostser_rxbuf[ rxbuf_pos-2 ];
	recv_crc |= hostser_rxbuf[ rxbuf_pos-1 ] << 8;

	if( crc == recv_crc ) {
		/* We have a valid frame :-O */
		rxed_frame = true;
		if( hostser_conf.rx_cb != NULL )
			hostser_conf.rx_cb();
	}
		
	rxbuf_pos = 0;
	/* XXX - this is wrong. But not importantly wrong right now */
	rxbuf_idx = (rxbuf_idx + 1) & 1;
	hostser_rxbuf = &rxbuf[rxbuf_idx][0];
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
	rxed_frame = false;
}

bool hostser_tx_busy( void )
{
	return txing_frame;
}

void hostser_tx( void )
{
	if( hostser_tx_busy() ) {
		/* Panic :-O This must never happen*/
		while(1);
	}

	tx_set_crc();
	hostser_txlen = SRIC_OVERHEAD + hostser_txbuf[ SRIC_LEN ];

	txbuf_pos = 0;
	txing_frame = true;

	/* Change outside view of where tx buffer is */
	hostser_txbuf = &txbuf[txbuf_idx][0];

	/* Actually begin transmission */
	hostser_conf.usart_tx_start( hostser_conf.usart_tx_start_n );
}
