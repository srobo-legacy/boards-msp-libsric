#ifndef __GW_H
#define __GW_H
/* Host <-> SRIC 'gateway' */
#include "sric-if.h"
#include "sric.h"
#include "hostser.h"

void sric_gw_init( void );

/* Call when frame available on the host side */
void sric_gw_hostser_rx( void );

/* Called when host-side transmission has completed */
void sric_gw_hostser_tx_done( void );

/* Call when frame available on the SRIC side */
uint8_t sric_gw_sric_rx( const sric_if_t *iface );

#endif	/* __GW_H */
