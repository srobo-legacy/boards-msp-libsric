#include "token-dir.h"
#include <stdbool.h>
#include <io.h>
#include <drivers/pinint.h>
#include <drivers/sched.h>

static bool have_token;
static bool requested;

#define to_low() do { (*token_dir_conf.to_port) &= ~token_dir_conf.to_mask; } while (0)
#define to_high() do { (*token_dir_conf.to_port) |= token_dir_conf.to_mask; } while (0)

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
	if(have_token)
		emit_token();

	have_token = false;
	requested = false;
}

static void cancel_req( void )
{
	release();
}

static bool emit_delayed(void *ud)
{
	emit_token();
	return false;
}

static sched_task_t emit_timeout = {
	.cb = emit_delayed,
};

static void token_isr(uint16_t flags)
{
	if( have_token )
		/* TODO: Deal with this situation. */
		while(1);

	if( requested ) {
		have_token = true;
		requested = false;
		token_dir_conf.haz_token();
	} else {
		/* Pass it on after a small delay */
		emit_timeout.t = 2;
		sched_add(&emit_timeout);
	}
}

const token_drv_t token_dir_drv = {
	.req = req,
	.cancel_req = cancel_req,
	.release = release,
};

static pinint_conf_t token_int;

void token_dir_init( void )
{
	have_token = false;
	requested = false;

	to_high();
	*token_dir_conf.to_dir |= token_dir_conf.to_mask;
	*token_dir_conf.ti_dir &= ~token_dir_conf.ti_mask;

	token_int.mask = token_dir_conf.ti_mask;
	token_int.int_cb = token_isr;
	/* mmm... hacky */
	if( token_dir_conf.ti_port == &P2IN )
		token_int.mask <<= 8;
	pinint_add( &token_int );

	/* Low-to-high interrupts please (more hackyness) */
	if( token_dir_conf.ti_port == &P1IN ) {
		P1IES &= ~token_dir_conf.ti_mask;
		P1IE |= token_dir_conf.ti_mask;
	} else {
		P2IES &= ~token_dir_conf.ti_mask;
		P2IE |= token_dir_conf.ti_mask;
	}
}

void token_dir_emit_first( void )
{
	emit_token();
}

bool token_dir_have_token( void )
{
	return have_token;
}
