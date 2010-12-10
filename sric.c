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
#include <signal.h>
#include <sys/cdefs.h>
#include "crc16.h"
#include <drivers/sched.h>

/* One additional byte for the 0x7e for correct stop bit receivage */
uint8_t sric_txbuf[SRIC_TXBUF_SIZE+1];
uint8_t sric_txlen;
static bool expect_resp;

/* Number of token loops to retransmit after */
#define TOKEN_THRESHOLD 3
/* Number of times the token's been seen this loop */
static uint8_t token_count;

static struct {
	/* Next byte to be transmitted */
	uint8_t out_pos;
} tx;

typedef enum {
	RX_IDLE,
	RX_HAVE_FRAME,
	RX_FULL
} rx_state_t;

typedef enum {
	EV_RX_RXED_FRAME,
	EV_RX_HANDLED_FRAME
} rx_event_t;

static uint8_t rxbuf[2][SRIC_RXBUF_SIZE + 1];
uint8_t *sric_w_rxbuf = &rxbuf[0][0];
uint8_t *sric_rxbuf = &rxbuf[0][0];
static uint8_t rxbuf_read_idx = 0;	/* Which rxbuf we're reading out of */
static uint8_t rxbuf_pos;
static volatile rx_state_t rx_state = RX_IDLE;

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

#define INTR_TIMEOUT		1
#define INTR_TX_COMPLETE	2
#define INTR_HAZ_TOKEN		4
static volatile uint8_t intr_flags = 0;

static void sric_tx_lock( void );
static void sric_tx_start( uint8_t len, bool expect_resp );
static void use_token( bool use );
static void sric_ctl( sric_ctl_t c );

sric_if_t sric_if = {
	.txbuf = sric_txbuf,
	.rxbuf = &rxbuf[0][0],
	.tx_lock = sric_tx_lock,
	.tx_cmd_start = sric_tx_start,
	.use_token = use_token,
	.ctl = sric_ctl,
};

static void fsm( event_t ev );
static void rx_fsm ( rx_event_t ev );

#define lvds_tx_en() do { (*sric_conf.txen_port) |= sric_conf.txen_mask; } while(0)
#define lvds_tx_dis() do { (*sric_conf.txen_port) &= ~sric_conf.txen_mask; } while(0)

void sric_init( void )
{
	if( SRIC_DIRECTOR ) {
		sric_addr = 1;
	} else {
		sric_addr = 0;
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
	sric_conf.usart_rx_gate(sric_conf.usart_n, false);
	lvds_tx_en();
	tx.out_pos = 0;
	sric_conf.usart_tx_start(sric_conf.usart_n);
}

/* Called in intr context */
static bool timeout( void *ud )
{
	intr_flags |= INTR_TIMEOUT;
	return false;
}

static bool sric_use_token = false;
static bool sric_use_token_buffered = false;
static bool sric_reset_queued = false;

static void register_timeout( void )
{
	/* Setup a long timeout for the response */
	if( sric_use_token )
		timeout_task.t = 15000;
	else
		timeout_task.t = 50;

	timeout_task.cb = timeout;
	sched_add(&timeout_task);
}

static void proc_queued_reset( void )
{
	if( sric_reset_queued ) {
		/* It's enumeration time */
		sric_reset_queued = false;

		/* Move to tokenless mode */
		sric_use_token_buffered = false;

		/* Throw away our address */
		sric_addr = 0;

		/* Request the token for enumeration */
		sric_conf.token_drv->req();
	}
}

static void fsm( event_t ev )
{
	switch(state) {
	case S_IDLE:
		if( ev == EV_TX_LOCK || ev == EV_RX ) {
			if( sric_use_token_buffered != sric_use_token )
				sric_use_token = sric_use_token_buffered;
		}

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

				if( sric_use_token ) {
					sric_conf.token_drv->req();
					state = S_TX_RESP_WAIT_TOKEN;
				} else {
					start_tx();
					state = S_TX_RESP;
				}
			} else
				proc_queued_reset();
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

			if( sric_use_token &&
					!sric_conf.token_drv->have_token()) {
				sric_conf.token_drv->req();
				state = S_TX_WAIT_TOKEN;
			} else {
				/* Start transmission immediately */
				token_count = 0;
				start_tx();
				state = S_TX;
			}
		}
		break;

	case S_TX_WAIT_TOKEN:
		if( ev == EV_GOT_TOKEN ) {
			/* Register timeout to reset in the event of waiting too long for the token */
			register_timeout();
			token_count = 0;
			start_tx();
			state = S_TX;
		}
		break;

	case S_TX:
		/* Transmitting a frame */
		if(ev == EV_TX_DONE) {
			sric_conf.usart_rx_gate(sric_conf.usart_n, true);

			if( sric_use_token )
				sric_conf.token_drv->release();

			if ( !expect_resp ) {
				/* No response expected */
				uint8_t i;

				if( sric_conf.rx_resp != NULL ) {
					/* Clear the rxbuf to ensure our "user" doesn't get confused... */
					for( i=0; i<SRIC_RXBUF_SIZE; i++ )
						sric_rxbuf[i] = 0;

					sric_conf.rx_resp( &sric_if );
				}

				state = S_IDLE;

			} else if( sric_use_token ) {
				/* Re-request the token for retransmission */
				sric_conf.token_drv->req();

				state = S_WAIT_RESP;

			} else {
				/* Register timeout for retransmission */
				register_timeout();

				state = S_WAIT_RESP;
			}

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
			if( sric_use_token )
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
			if( sric_use_token )
				sric_conf.token_drv->cancel_req();

			if( sric_conf.rx_resp != NULL )
				sric_conf.rx_resp( &sric_if );

			state = S_IDLE;
		} else if( ev == EV_TIMEOUT ) {
			if( sric_use_token ) {
				/* We've spent too long waiting for a response */
				/* Abort the whole situation */

				/* Drop our token request */
				sric_conf.token_drv->cancel_req();

				/* Emit the error callback */
				if( sric_conf.error != NULL )
					sric_conf.error();

				state = S_IDLE;
			} else {
				/* Retransmit time */
				start_tx();

				/* TODO: Abort after N retransmissions */
				state = S_TX;
			}

		} else if( ev == EV_GOT_TOKEN && sric_use_token ) {
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
			sric_conf.usart_rx_gate(sric_conf.usart_n, true);
			if( sric_use_token )
				sric_conf.token_drv->release();

			proc_queued_reset();
			state = S_IDLE;
		}
		break;

	default:
		state = S_IDLE;
	}
}

/* Called in intr context */
bool sric_tx_cb( uint8_t *b )
{
	static bool escape_next = false;

	if( tx.out_pos == sric_txlen ) {
		/* As per the srobo-devel@ list on 09/12/2010, some death
		 * occurs at the end of transmission if we release the token
		 * on the "transmit buffer empty" intr - we need to wait for
		 * the last bit and stop bit to get out. So:
		 *
		 * 1) Send two buffer bytes at end of transmission
		 * 2) Cut it off at the knees by disabling txmission in the
		 *    empty buffer intr
		 * 3) at next buffer empty intr, we know a clean stop bit got
		 *    out and the bus is idle from everyone elses perspective
		 */
		*b = 0xFF;
		tx.out_pos++;
		return true;
	} else if ( tx.out_pos == sric_txlen + 1 ) {

		lvds_tx_dis();

		*b = 0xFF;
		tx.out_pos++;
		return true;
	} else if( tx.out_pos == sric_txlen + 2) {
		/* Transmission complete */
		intr_flags |= INTR_TX_COMPLETE;
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

/* Called in intr context */
void sric_rx_cb( uint8_t b )
{
	static bool escape_next = false;
	uint8_t len;

	if ( rx_state == RX_FULL )
		/* Both buffers are full, discard new data */
		return;

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

	sric_w_rxbuf[rxbuf_pos] = b;
	rxbuf_pos += 1;

	if( sric_w_rxbuf[0] != 0x7e
	    /* Make sure we've reached the minimum frame size */
	    || rxbuf_pos < (SRIC_LEN + 2) )
		return;

	len = sric_w_rxbuf[SRIC_LEN];
	if( len != rxbuf_pos - (SRIC_LEN + 3) )
		return;

	/* We have a frame :-O */
	rx_fsm( EV_RX_RXED_FRAME );

	rxbuf_pos = 0;
}

static void sric_tx_lock( void )
{
	while( state != S_TX_LOCKED )
		fsm(EV_TX_LOCK);
}

static void sric_tx_start( uint8_t len, bool _expect_resp )
{
	sric_txlen = len;
	expect_resp = _expect_resp;

	fsm(EV_TX_START);
}

/* Called in intr context */
void sric_haz_token( void )
{
	intr_flags |= INTR_HAZ_TOKEN;
}

static void use_token( bool use )
{
	sric_use_token_buffered = use;
}

static void sric_ctl ( sric_ctl_t c )
{
	switch(c)
	{
	case SRIC_CTL_RESET:
		sric_reset_queued = true;
		break;

	case SRIC_CTL_RELEASE_TOK:
		sric_conf.token_drv->release();
		break;

	case SRIC_CTL_REQUEST_TOK:
		sric_conf.token_drv->req();
		break;
	}
}

void sric_poll( void )
{
#define DISABLE_FLAG(n) do { dint(); intr_flags &= ~(n); eint(); } while (0)
	if (intr_flags & INTR_TIMEOUT) {
		DISABLE_FLAG(INTR_TIMEOUT);
		fsm( EV_TIMEOUT );
	}

	if (intr_flags & INTR_TX_COMPLETE) {
		DISABLE_FLAG(INTR_TX_COMPLETE);
		fsm( EV_TX_DONE );
	}

	if (rx_state == RX_FULL || rx_state == RX_HAVE_FRAME) {
		/* First, check crc */
		uint16_t crc, recv_crc;
		uint8_t len;
		len = sric_rxbuf[ SRIC_LEN ];
		crc = crc16( sric_rxbuf, SRIC_DATA + len );

		recv_crc = sric_rxbuf[ SRIC_DATA + len ];
		recv_crc |= sric_rxbuf[ SRIC_DATA + len + 1 ] << 8;

		if (crc == recv_crc) {
#ifdef SRIC_PROMISC
			sric_conf.promisc_rx(&sric_if);
#endif
			fsm( EV_RX );
		}

		/* Update srics view of where the input buffer is */
		rxbuf_read_idx ^= 1;
		sric_rxbuf = &rxbuf[rxbuf_read_idx][0];
		sric_if.rxbuf = sric_rxbuf;

		dint();
		rx_fsm( EV_RX_HANDLED_FRAME );
		eint();
	}

	if (intr_flags & INTR_HAZ_TOKEN) {
		DISABLE_FLAG(INTR_HAZ_TOKEN);
		fsm( EV_GOT_TOKEN );
	}
#undef DISABLE_FLAG
}

/* Called in intr context */
static void rx_fsm ( rx_event_t ev )
{

	switch ( (int) rx_state ) {
	case RX_IDLE:
		if ( ev == EV_RX_RXED_FRAME ) {
			if (sric_w_rxbuf < &rxbuf[1][0])
				sric_w_rxbuf = &rxbuf[1][0];
			else
				sric_w_rxbuf = &rxbuf[0][0];

			rx_state = RX_HAVE_FRAME;
		}
		break;

	case RX_HAVE_FRAME:
		if ( ev == EV_RX_RXED_FRAME ) {
			rx_state = RX_FULL;
		} else if ( ev == EV_RX_HANDLED_FRAME ) {
			rx_state = RX_IDLE;
		}
		break;

	case RX_FULL:
		if ( ev == EV_RX_HANDLED_FRAME ) {
			/* Incoming data goes into the other buffer */
			if (sric_w_rxbuf < &rxbuf[1][0])
				sric_w_rxbuf = &rxbuf[1][0];
			else
				sric_w_rxbuf = &rxbuf[0][0];

			rx_state = RX_HAVE_FRAME;
		}
		break;
	}
}
