/*   Copyright (C) 2010 Robert Spanton, Richard Barlow

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
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

	gt_low();

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
