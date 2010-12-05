#ifndef __HOSTSER_H
#define __HOSTSER_H
/* Routines for managing interaction with the host device 
   (e.g. BeagleBoard or PC) over serial link. */
#include <stdbool.h>
#include <stdint.h>
#include "sric.h"

#define HOSTSER_BUF_SIZE SRIC_TXBUF_SIZE
/* Transmit buffer
   All bytes except the first are escaped as they leave. */
extern uint8_t *hostser_txbuf;
/* Number of bytes in the transmit buffer */
extern uint8_t hostser_txlen;
/* Receive buffer */
extern uint8_t *hostser_rxbuf;

/* An instance of this struct must be linked in, and named
   hostser_conf.  Should be const. */
typedef struct {
	/* Function to be called to start the USART transmitting */
	void (*usart_tx_start) (uint8_t n);

	/* n to pass to the start function */
	uint8_t usart_tx_start_n;

	/* Called when a frame has been received and is in the receive buffer */
	void (*rx_cb) ( void );

	/* Called when transmission of a frame has completed */
	void (*tx_done_cb) ( void );
} hostser_conf_t;

void hostser_init( void );

/* Transmit byte generator */
bool hostser_tx_cb( uint8_t *b );
/* Callback for each byte received */
void hostser_rx_cb( uint8_t b );

/* Request that the given frame is transmitted
   Sorts out CRC.
   Must be called when the tx is not busy. */
void hostser_tx( void );

/* Returns true when the tx is busy */
bool hostser_tx_busy( void );

/* Indicate that the received frame has been processed */
void hostser_rx_done( void );

#endif	/* __HOSTSER_H */
