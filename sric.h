#ifndef __SRIC_H
#define __SRIC_H
#include <stdbool.h>
#include <stdint.h>
#include <io.h>
#include "sric-if.h"
#include "token-drv.h"

#define MAX_PAYLOAD 64
#define MAX_FRAME_LEN MAX_PAYLOAD + 6

#define SRIC_TXBUF_SIZE MAX_FRAME_LEN
#define SRIC_RXBUF_SIZE SRIC_TXBUF_SIZE
/* The transmit buffer */
extern uint8_t sric_txbuf[];
/* Number of bytes in the transmit buffer */
extern uint8_t sric_txlen;

extern uint8_t *sric_rxbuf;

/* Offsets of fields in the tx buffer */
enum {
	SRIC_DEST = 1,
	SRIC_SRC = 2,
	SRIC_LEN = 3,
	SRIC_DATA = 4
	/* CRC is last two bytes */
};

/* The number of bytes in a SRIC header */
#define SRIC_HEADER_SIZE 4

/* The number of bytes in the header and footer of a SRIC frame */
#define SRIC_OVERHEAD (SRIC_HEADER_SIZE + 2)

#define sric_addr_set_ack(x) (x | 0x80)
#define sric_addr_is_ack(x) ( x & 0x80 )
#define sric_frame_is_ack(buf) ( sric_addr_is_ack(buf[SRIC_DEST]) )
#define sric_frame_set_ack(buf) do { buf[SRIC_DEST] = sric_addr_set_ack(buf[SRIC_DEST]); } while (0)

/**** Special return values for the command rx callback to return: *****/
/* Respond now, regardless of token posession. Is a flag bit */
#define SRIC_RESPOND_NOW 128
/* The response will be provided to the interface later */
#define SRIC_RESPONSE_DEFER 127
/* Do not respond to this command */
#define SRIC_IGNORE 126
/* Smallest special return - used to identify when we've received a special
 * response and need to pass it back up the call chain */
#define SRIC_SPECIAL_RET_LIMIT SRIC_IGNORE

/* Mask to extract length from rx callback */
#define SRIC_LENGTH_MASK	0x7F

/* SRIC configuration
   There must be a const instance of this called sric_conf somewhere. */
typedef struct {
	/* Function to be called to start the USART transmitting */
	void (*usart_tx_start) (uint8_t n);

	/* USART enable/disable function */
	void (*usart_rx_gate) (uint8_t n, bool en);

	/* n to pass to the usart functions */
	uint8_t usart_n;

	const token_drv_t *token_drv;

	/* Registers and bitmask for controlling the TXEN line */
	typeof(P1DIR) *txen_dir;
	typeof(P1OUT) *txen_port;
	uint8_t txen_mask;

	/*** Callbacks ***/
	/* Received a command frame.
	   This callback can be used in two ways:
	   1) It can assemble the response frame in the transmit buffer
	      and return the number of bytes it put there (from the start 
	      of the buffer to the last data byte).  The interface will
	      then begin transmission of that response immediately.

	   2) It can return SRIC_RESPONSE_DEFER, in which case the interface
	      will wait for the response frame to be provided with a call to 
	      tx_response (which takes the number of bytes etc.). */
	uint8_t (*rx_cmd) ( const sric_if_t *iface );

	/* Received a response frame
	   Only called if not NULL.
	   Called upon transmission completion when expect_resp is false. */
	void (*rx_resp) ( const sric_if_t *iface );

	/* Error: Called when a timeout occurs
	   The interface resets itself when this happens. */
	void (*error) (void);

#if SRIC_PROMISC
	/* Called when a frame is received -- regardless of cmd or response */
	void (*promisc_rx) ( const sric_if_t *iface );
#endif
} sric_conf_t;

extern const sric_conf_t sric_conf;

/* Our SRIC address */
/* 0 means we haven't had one assigned yet */
extern uint8_t sric_addr;

/* Description of this interface */
extern sric_if_t sric_if;

/* System command constants */
enum {
	SRIC_SYSCMD_RESET,
	SRIC_SYSCMD_TOK_ADVANCE,
	SRIC_SYSCMD_ADDR_ASSIGN,
	SRIC_SYSCMD_ADDR_INFO,
};

/* Initialise the internal goo */
void sric_init( void );

/* Transmit byte generator */
bool sric_tx_cb( uint8_t *b );
/* Callback for each byte received */
void sric_rx_cb( uint8_t b );
/* Callback for got the token */
void sric_haz_token( void );

/* Sric routines to poll for activity caused by interrupts: */
void sric_poll( void );
/* Host serial events */
void hostser_poll( void );

#endif	/* __SRIC_H */
