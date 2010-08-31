#ifndef __TOKEN_MSP_H
#define __TOKEN_MSP_H
/* Token client driver for an MSP that lacks a 10F to do it for it.
   (e.g. a pc-sric board in pass-through mode) */
#include "token-drv.h"
#include <io.h>
#include <stdint.h>

typedef struct {
	void (*haz_token) (void);

	/* TO Pin: */
	typeof(P1OUT) *to_port;
	typeof(P1DIR) *to_dir;
	uint8_t to_mask;

	/* TI Pin: */
	typeof(P1IN) *ti_port;
	typeof(P1DIR) *ti_dir;
	uint8_t ti_mask;
} token_msp_conf_t;

extern const token_msp_conf_t token_msp_conf;
extern const token_drv_t token_msp_drv;

void token_msp_init( void );

#endif	/* __TOKEN_H */
