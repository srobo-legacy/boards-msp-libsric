#ifndef __SRIC_CLIENT_H
#define __SRIC_CLIENT_H
#include <stdint.h>
#include "sric-if.h"

typedef struct {
	/* Function to call when a command is received.
	   Find the received command in iface->rxbuf.
	   Place the response in iface->txbuf, then return the number
	   of bytes in the buffer.
	   (This will be transmitted as a response). */
	uint8_t (*cmd) ( const sric_if_t *iface );
} sric_cmd_t;

/* The command table -- obviously specific to each device */
extern const sric_cmd_t sric_commands[];
/* Number of commands */
extern const uint8_t sric_cmd_num;

void sric_client_init( void );

/* Callback for received SRIC frames */
uint8_t sric_client_rx( const sric_if_t *iface );

#endif	/* __SRIC_CLIENT_H */
