#ifndef __TOKEN_DRV_H
#define __TOKEN_DRV_H
#include <stdbool.h>

typedef struct {
	/* Request the token */
	void (*req) (void);

	/* Cancel a request for the token
	   (if we already have the token, it is released) 
	   Safe to call if there was no previous request. */
	void (*cancel_req) (void);

	/* Release the token */
	void (*release) (void);

	/* Returns true if we currently have the token */
	bool (*have_token) (void);
} token_drv_t;

#endif	/* __TOKEN_DRV_H */
