#include "token-dummy.h"
#include <drivers/sched.h>
#include <stdint.h>

extern const token_dummy_conf_t token_dummy_conf;
static uint16_t delay;

static bool timeout( void *ud )
{
	token_dummy_conf.haz_token();
	return false;
}

static sched_task_t timeout_task = 
{
	.t = 0,
	.cb = timeout,
	.udata = NULL,
};

void token_dummy_init( uint16_t _delay )
{
	delay = _delay;
}

static void req( void )
{
	timeout_task.t = delay;
	sched_add(&timeout_task);
}

static void cancel_req( void )
{
	sched_rem(&timeout_task);
}

static void release( void )
{
	/* Nothing to do! */
}

static bool have_token( void )
{
	/* Pretend to always have the token... */
	/* This won't do what you want when enumerating... but this driver doesn't
	 really do what you want anyway! */
	return true;
}

const token_drv_t token_dummy_drv = {
	.req = req,
	.cancel_req = cancel_req,
	.release = release,
	.have_token = have_token,
};
