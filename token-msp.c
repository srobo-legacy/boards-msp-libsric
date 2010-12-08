#include "token-msp.h"
#include <signal.h>
#include <stdbool.h>
#include <io.h>
#include <drivers/pinint.h>

static bool have_token;
static bool requested;

#define to_low() do { (*token_msp_conf.to_port) &= ~token_msp_conf.to_mask; } while (0)
#define to_high() do { (*token_msp_conf.to_port) |= token_msp_conf.to_mask; } while (0)

static void emit_token( void )
{
	uint8_t i;
	to_low();

	/* Yea, it's a long time -- will require some adjustment */
	for(i=0; i<64;i++)
		nop();

	to_high();
}

static void req( void )
{
	requested = true;
}

static void release( void )
{
	requested = false;

	if(have_token) {
		have_token = false;
		emit_token();
	}
}

static void cancel_req( void )
{
	release();
}

static bool get_have_token( void )
{
	return have_token;
}

static void token_isr(uint16_t flags)
{
	if( have_token )
		/* Ignore duplicate tokens */
		return;

	if( requested ) {
		have_token = true;
		requested = false;
		token_msp_conf.haz_token();
	} else
		emit_token();
}

static pinint_conf_t token_int;

void token_msp_init( void )
{
	have_token = false;
	requested = false;

	to_high();
	*token_msp_conf.to_dir |= token_msp_conf.to_mask;
	*token_msp_conf.ti_dir &= ~token_msp_conf.ti_mask;

	token_int.mask = token_msp_conf.ti_mask;
	token_int.int_cb = token_isr;
	/* mmm... hacky */
	if( token_msp_conf.ti_port == &P2IN )
		token_int.mask <<= 8;
	pinint_add( &token_int );

	/* Low-to-high interrupts please (more hackyness) */
	if( token_msp_conf.ti_port == &P1IN ) {
		P1IES &= ~token_msp_conf.ti_mask;
		P1IE |= token_msp_conf.ti_mask;
	} else {
		P2IES &= ~token_msp_conf.ti_mask;
		P2IE |= token_msp_conf.ti_mask;
	}

	emit_token();
}

const token_drv_t token_msp_drv = {
	.req = req,
	.cancel_req = cancel_req,
	.release = release,
	.have_token = get_have_token,
};
