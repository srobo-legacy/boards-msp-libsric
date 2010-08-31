#ifndef __TOKEN_10F_H
#define __TOKEN_10F_H
/* Token client driver for normal boards that have a PIC 10F200 */
#include "token-drv.h"
#include <io.h>
#include <stdint.h>

typedef struct {
	void (*haz_token) (void);

	/* GT Pin: */
	typeof(P1OUT) *gt_port;
	typeof(P1DIR) *gt_dir;
	uint8_t gt_mask;

	/* HT Pin: */
	typeof(P1IN) *ht_port;
	typeof(P1DIR) *ht_dir;
	uint8_t ht_mask;
} token_10f_conf_t;

extern const token_10f_conf_t token_10f_conf;
extern const token_drv_t token_10f_drv;

void token_10f_init( void );

#endif	/* __TOKEN_10F_H */
