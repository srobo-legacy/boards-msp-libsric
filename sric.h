#ifndef __SRIC_H
#define __SRIC_H
#include <stdbool.h>
#include <stdint.h>

#define MAX_PAYLOAD 64

#define SRIC_TXBUF_SIZE (6 + MAX_PAYLOAD)
#define SRIC_RXBUF_SIZE SRIC_TXBUF_SIZE
/* The transmit buffer */
extern uint8_t sric_txbuf[];
/* Number of bytes in the transmit buffer */
extern uint8_t sric_txlen;

extern uint8_t sric_rxbuf[];

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

/* SRIC configuration
   There must be a const instance of this called sric_conf somewhere. */
typedef struct {
	/* Function to be called to start the USART transmitting */
	void (*usart_tx_start) (uint8_t n);

	/* USART enable/disable function */
	void (*usart_rx_gate) (uint8_t n, bool en);

	/* n to pass to the usart functions */
	uint8_t usart_n;
} sric_conf_t;

/* Our SRIC address */
/* 0 means we haven't had one assigned yet */
extern uint8_t sric_addr;

/* Initialise the internal goo */
void sric_init( void );

/* Transmit byte generator */
bool sric_tx_cb( uint8_t *b );
/* Callback for each byte received */
void sric_rx_cb( uint8_t b );

/* Begin transmission of the frame in the tx buffer */
void sric_tx( void );

/* Transmit a response frame */
void sric_tx_resp( void );

/* Indicate that the received frame has been processed */
void sric_rx_done( void );

#endif	/* __SRIC_H */
