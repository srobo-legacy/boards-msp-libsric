#ifndef __TOKEN_DUMMY_H
#define __TOKEN_DUMMY_H
/* Token client driver that uses timeouts to emulate the token
   Mainly useful for debugging purposes. */
#include "token-drv.h"
#include <stdint.h>

typedef struct {
	void (*haz_token) (void);
} token_dummy_conf_t;

extern const token_drv_t token_dummy_drv;

void token_dummy_init( uint16_t delay );

#endif	/* __TOKEN_DUMMY_H */
