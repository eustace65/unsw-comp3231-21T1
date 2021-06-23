#ifndef CLIENT_SERVER_H
#define CLIENT_SERVER_H

/*  This file contains constants, types, and prototypes for the
 *  client/server system. It is included by the both tester and the
 *  file you modify so as to share the definitions between both.

 *  YOU SHOULD NOT CHANGE THIS FILE

 *  We will replace it in testing, so any changes will be lost.
 */



/* This is a type definition of the request structure. You will be
 * passing pointers to this type in your own data structures, but you
 * can't assume anything about its internal structure.
 */
typedef struct request {
        struct semaphore *done;
        unsigned number;
        unsigned check;
} request_t;


/* Prototype for the interface you will implement. See the provided
   skeleton for more details. */

extern int work_queue_setup(void);
extern void work_queue_shutdown(void);   
extern void work_queue_enqueue(request_t *request);
extern request_t *work_queue_get_next(void);

#endif
