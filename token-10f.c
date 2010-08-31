#include "token-10f.h"
#include <io.h>
#include <drivers/pinint.h>

#define gt_low() do { (*token_10f_conf.gt_port) &= ~token_10f_conf.gt_mask; } while (0)
#define gt_high() do { (*token_10f_conf.gt_port) |= token_10f_conf.gt_mask; } while (0)

static void req( void )
{
	gt_high();
}

static void release( void )
{
	gt_low();
}

static void cancel_req( void )
{
	release();
}

static void token_isr(uint16_t flags)
{
	token_10f_conf.haz_token();
}

static pinint_conf_t token_int;

void token_10f_init( void )
{
	*token_10f_conf.gt_dir |= token_10f_conf.gt_mask;
	*token_10f_conf.ht_dir &= ~token_10f_conf.ht_mask;

	token_int.mask = token_10f_conf.ht_mask;
	token_int.int_cb = token_isr;
	/* mmm... hacky */
	if( token_10f_conf.ht_port == &P2IN )
		token_int.mask <<= 8;
	pinint_add( &token_int );

	/* Low-to-high interrupts please (more hackyness) */
	if( token_10f_conf.ht_port == &P1IN ) {
		P1IES &= ~token_10f_conf.ht_mask;
		P1IE |= token_10f_conf.ht_mask;
	} else {
		P2IES &= ~token_10f_conf.ht_mask;
		P2IE |= token_10f_conf.ht_mask;
	}
}

const token_drv_t token_10f_drv = {
	.req = req,
	.cancel_req = cancel_req,
	.release = release,
};
