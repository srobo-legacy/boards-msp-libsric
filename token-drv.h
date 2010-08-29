#ifndef __TOKEN_DRV_H
#define __TOKEN_DRV_H

typedef struct {
	/* Request the token */
	void (*req) (void);

	/* Cancel a request for the token
	   (if we already have the token, it is released) 
	   Safe to call if there was no previous request. */
	void (*cancel_req) (void);

	/* Release the token */
	void (*release) (void);
} token_drv_t;

#endif	/* __TOKEN_DRV_H */
