/* This file will contain your solution. Modify it as you wish. */
// a linked list is used to implement the queue
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <kern/errno.h>
#include "client_server.h"

/*
 * Declare any variables you need here to implement and
 *  synchronise your queues and/or requests.
 */
struct queue{
        struct node* head;
        struct node* tail;
        struct node* current;
};

struct node{
        request_t *request;
        struct node *next;
};
struct cv *empty;
struct queue *new_queue;
struct lock *new_lock;

/* work_queue_enqueue():
 *
 * req: A pointer to a request to be processed. You can assume it is
 * a valid pointer or NULL. You can't assume anything about what it
 * points to, i.e. the internals of the request type.
 *
 * This function is expected to add requests to a single queue for
 * processing. The queue is a queue (FIFO). The function then returns
 * to the caller. It can be called concurrently by multiple threads.
 *
 * Note: The above is a high-level description of behaviour, not
 * detailed psuedo code. Depending on your implementation, more or
 * less code may be required.
 */



void work_queue_enqueue(request_t *req)
{
        lock_acquire(new_lock);
        // add request to a node in the queue
        struct node *new_node = kmalloc(sizeof(struct node));
        new_node->request = req;
        new_node->next = NULL;
        if (new_queue->head == NULL)
                new_queue->head = new_node;
        if (new_queue->current == NULL)
                new_queue->current = new_node;
        if (new_queue->tail != NULL)
                new_queue->tail->next = new_node;
        new_queue->tail = new_node;
        cv_signal(empty, new_lock);
        lock_release(new_lock);
}

/*
 * work_queue_get_next():
 *
 * This function is expected to block on a synchronisation primitive
 * until there are one or more requests in the queue for processing.
 *
 * A pointer to the request is removed from the queue and returned to
 * the server.
 *
 * Note: The above is a high-level description of behaviour, not
 * detailed psuedo code. Depending on your implementation, more or
 * less code may be required.
 */


request_t *work_queue_get_next(void)
{
        lock_acquire(new_lock);
        // no requests waiting to be processed
        while(new_queue->current == NULL)
                cv_wait(empty, new_lock);
        request_t *req = new_queue->current->request;
        new_queue->current = new_queue->current->next;
        lock_release(new_lock);
        return req;
}




/*
 * work_queue_setup():
 *
 * This function is called before the client and server threads are started. It is
 * intended for you to initialise any globals or synchronisation
 * primitives that are needed by your solution.
 *
 * In returns zero on success, or non-zero on failure.
 *
 * You can assume it is not called concurrently.
 */

int work_queue_setup(void)
{
        new_queue = kmalloc(sizeof(struct queue));
        empty = cv_create("empty");
        new_lock = lock_create("new_lock");
        if (empty == NULL || new_lock == NULL || new_queue == NULL)
                return ENOSYS;
        // initialize queue
        new_queue->head = NULL;
        new_queue->tail = NULL;
        new_queue->current = NULL;
        return 0;
}


/*
 * work_queue_shutdown():
 *
 * This function is called after the participating threads have
 * exited. Use it to de-allocate or "destroy" anything allocated or created
 * on setup.
 *
 * You can assume it is not called concurrently.
 */

void work_queue_shutdown(void)
{
        cv_destroy(empty);
        lock_destroy(new_lock);
        // free the node in queue
        while (new_queue->head != NULL) {
                struct node *curr = new_queue->head;
                new_queue->head = new_queue->head->next;
                kfree(curr);
        }
        kfree(new_queue);

}