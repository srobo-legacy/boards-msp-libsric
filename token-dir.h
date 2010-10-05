#ifndef __TOKEN_DIR_H
#define __TOKEN_DIR_H
/* Token driver for master.
   In addition to token passing/requesting:
   * Initial token generation
   * Token loss detection and regeneration */
#include "token-drv.h"
#include <io.h>
#include <stdbool.h>
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
} token_dir_conf_t;

extern const token_dir_conf_t token_dir_conf;
extern const token_drv_t token_dir_drv;

void token_dir_init( void );

/* Emit the first token */
void token_dir_emit_first( void );

/* Returns true if we have the token */
bool token_dir_have_token( void );

#endif	/* __TOKEN_DIR_H */
