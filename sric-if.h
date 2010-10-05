#ifndef __SRIC_IF_H
#define __SRIC_IF_H
#include <stdint.h>
#include <stdbool.h>

/* Struct describing a SRIC interface */
typedef struct {
	/* Transmit and receive buffers */
	uint8_t *txbuf, *rxbuf;

	/* Function to acquire lock on transmit buffer
	   Blocks until lock acquired.
	   Must only be called prior to transmitting a command
	   (the buffer is normally already locked when the interface is
	   expecting a response) */
	void (*tx_lock) (void);

	/* Function to transmit the contents of the buffer
	   len is the number of bytes in the buffer up to the end of the data field
	   (the interface will manage checksumming) */
	void (*tx_cmd_start) ( uint8_t len );

	/* Start transmitting a response frame
	   Must only be called when the rx_cmd callback has returned
	   SRIC_RESPONSE_DEFER. */
	void (*tx_response) ( uint8_t len );

	/* Set whether to use the token.
	    true: Normal operation with token.
	   false: Timeout based operation -- for initialisation. */
	void (*use_token) ( bool use );
} sric_if_t;

#endif	/* __SRIC_IF_H */
