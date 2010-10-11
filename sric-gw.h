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

/* Called with incoming sric command frames */
uint8_t sric_gw_sric_rxcmd( const sric_if_t *iface );

/* Called with incoming sric response frames */
void sric_gw_sric_rxresp( const sric_if_t *iface );

/* Called when sric interface experiences an error (timeout) */
void sric_gw_sric_err( void );

/* Enumerate the bus etc */
void sric_gw_init_bus( void );

#endif	/* __GW_H */
