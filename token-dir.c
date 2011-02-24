#include "token-dir.h"
#include <signal.h>
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

static bool emit_delayed(void *ud)
{
	void **ud2;

	emit_token();
	ud2 = ud;
	*ud2 = NULL;
	return false;
}

static sched_task_t emit_timeout = {
	.cb = emit_delayed,
	.udata = NULL,
};

static void token_isr(uint16_t flags)
{
	if( have_token )
		/* So, this occuring shows there are duplicate tokens on the
		 * bus. This sucks, and could have caused data corruption.
		 * However, there's nothing that can be done at this point
		 * which will make it any better, and in fact it's good that
		 * we can congeal two tokens into one by dropping one here. */
		return;

	if( requested ) {
		have_token = true;
		requested = false;
		token_dir_conf.haz_token();
	} else if (emit_timeout.udata == NULL) {
		/* Pass it on after a small delay */
		emit_timeout.t = 2;
		/* Some random pointer to allow us to see if we've already
		 * registered this callback */
		emit_timeout.udata = &emit_timeout.udata;
		sched_add(&emit_timeout);
	}
}

const token_drv_t token_dir_drv = {
	.req = req,
	.cancel_req = cancel_req,
	.release = release,
	.have_token = get_have_token,
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

