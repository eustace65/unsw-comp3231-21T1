/*
 * Tester file for the client/server system simulation.
 *
 * This starts up a number of threads and has them communicate via the
 * API defined (and implemented by you) in client_server.c.
 *
 * NOTE: DO NOT RELY ON ANY CHANGES YOU MAKE TO THIS FILE, BECAUSE IT
 * WILL BE OVERWRITTEN DURING TESTING.
 */
#include "opt-synchprobs.h"
#include <types.h>  /* required by lib.h */
#include <lib.h>    /* for kprintf */
#include <synch.h>  /* for P(), V(), sem_* */
#include <thread.h> /* for thread_fork() */
#include <test.h>

#include "client_server.h"

/* The number of client threads.  This will be changed during testing
 */
#define NUM_CLIENTS 10

/* The number of server threads.  This number will be changed during
 * testing
 */
#define NUM_SERVERS 3

/* Number of requests each client thread generates before
 * exiting. This number will be changed during testing.
 */
#define REQUESTS_TO_MAKE 50

/* Semaphores which the system tester uses to determine when all
 * client threads and all server threads have finished.
 */
static struct semaphore *servers_finished;
static struct semaphore *clients_finished;

/* keep some stats for sanity checking */
static int processed[NUM_SERVERS];

/********************************************************************************* 
 * The clients thread's function. This function calls
 * work_queue_enqueue REQUESTS_TO_MAKE times and then
 * exits. NUM_CLIENTS threads are started and each run the function.
 */

static void
client_thread(void *unused_ptr, unsigned long client_id)
{
        int requests_to_go = REQUESTS_TO_MAKE;
        request_t req;

        (void)unused_ptr; /* Avoid compiler warnings */

        kprintf("Client %ld started\n", client_id);

        /* allocate a single semaphore that we use for all request
           from this client. Each client only has one oustanding
           request at a time. */ 
        req.done = sem_create("Client sem",0); 
        
        if (req.done == NULL) {
                /* This should not happen and there is no reasonable
                   way to recover. */
                panic("Can't create a semaphore??");
        }
        
        while(requests_to_go > 0) {

                /* Initialise the request with a reasonably unique ID
                   number
                 */
                req.number = client_id * REQUESTS_TO_MAKE + requests_to_go - 1;
                req.check = 0;
                
                work_queue_enqueue(&req); /* send the request to the servers */

                P(req.done); /* wait for it to be processed */
                
                if (req.number != ~(req.check)) {
                        panic("My request is corrupt or invalid");
                }

                requests_to_go = requests_to_go - 1;
        }
        
        sem_destroy(req.done); /* we're finished, clean up */
        /* Signal that we're done. */
        kprintf("Client %ld finished\n", client_id);
        V(clients_finished);
}

/***********************************************************************
 * The server thread's function. NUM_SERVER threads are started, each
 * of which runs this function. The function continuously calls
 * work_queue_get_next() until it receives a special NULL request.
 */
static void
server_thread(void *unused_ptr, unsigned long server_id)
{
        request_t  *req;
        int delay;

        (void)unused_ptr;

        kprintf("Server %ld started\n", server_id);

        req = work_queue_get_next();

        while (req != NULL) {
                
                /* insert a random delay representing some kind of
                   processing or I/O */

                delay = random() % 16;
                for (int i = 0; i <= delay; i++) {
                        thread_yield();
                }

                /* process the request, which in this case is just
                   writing a check word that is a function of the
                   number in the request */
                req->check = ~(req->number); 

                processed[server_id]++;

                V(req->done); /* signal the request is done to the
                                 client */

                req = work_queue_get_next();
        }

        /* We got a NULL, so signal that we're done. */
        V(servers_finished);
}

/* Create a group of threads to serve requests. */
static void
start_server_threads()
{
        int i;
        int result;

        for(i = 0; i < NUM_SERVERS; i++) {
                processed[i]=0;
                result = thread_fork("server thread", NULL,
                                     server_thread, NULL, i);
                if(result) {
                        panic("start_server_threads: couldn't fork (%s)\n",
                              strerror(result));
                }
        }
}

/* Create a group of clients to make requests. */
static void
start_client_threads()
{
        int i;
        int result;

        for(i = 0; i < NUM_CLIENTS; i++) {
                result = thread_fork("client thread", NULL,
                                     client_thread, NULL, i);
                if(result) {
                        panic("start_client_threads: couldn't fork (%s)\n",
                              strerror(result));
                }
        }
}

/* 
 * Wait for all client threads to exit.  Clients each produce a number
 * of requests. When finished, they signal a semaphore and exit. To
 * wait for them all to finish, we wait on the semaphore NUM_CLIENTS
 * times.
 */
static void
wait_for_client_threads()
{
        int i;
        kprintf("Waiting for client threads to exit...\n");
        for(i = 0; i < NUM_CLIENTS; i++) {
                P(clients_finished);
        }
        kprintf("All %d client threads have exited.\n", NUM_CLIENTS);
}

/* 
 * Instruct all server threads to exit and then wait for them to
 * indicate that they have exited. Server threads run until told
 * to stop using the special NULL request.
 */

static void
stop_server_threads()
{
        int i;

        for(i = 0; i < NUM_SERVERS; i++) {
                /* 
                 * Our protocol for stopping server threads is to send
                 * NUM_SERVERS NULL requests.
                 */
                work_queue_enqueue(NULL);
        }

        /* Now wait for all servers to signal completion. */
        for(i = 0; i < NUM_SERVERS; i++) {
                P(servers_finished);
        }

        kprintf("All %d server threads have exited.\n", NUM_SERVERS);

}

/* The main function for the client/server system. */
int
run_client_server_system(int nargs, char **args)
{
        int check = 0;
        int i, error;
        (void) nargs; /* Avoid "unused variable" warnings */
        (void) args;
        
        kprintf("run_client_server_system: starting up\n");
        
        /* Initialise synch primitives used in the ticket system driver */
        servers_finished = sem_create("servers_finished", 0);
        if(servers_finished == NULL) {
                panic("system: couldn't create servers semaphore\n");
        }

        clients_finished = sem_create("clients_finished", 0);
        if(clients_finished == NULL) {
                panic("system: couldn't create clients semaphore\n");
        }
        
        /* Run any code required to initialise the work queue etc. */
        error = work_queue_setup();
        if (error) panic("work queue setup returned an error\n");

        /* Run the simulation */
        start_client_threads();
        start_server_threads();

        /* Wait for all ticket holders and validators to finish */
        wait_for_client_threads();
        stop_server_threads();

        /* Run any code required to shut down the work queue */
        work_queue_shutdown();

        for (i = 0;i < NUM_SERVERS;i++){
                check += processed[i];
                kprintf("Server %d processed %d requests\n", i, processed[i]);
        }
        kprintf("Giving a total of %d (expected %d)\n", check, NUM_CLIENTS * REQUESTS_TO_MAKE);
        
        /* Done! */
        sem_destroy(clients_finished);
        sem_destroy(servers_finished);
        return 0;
}
