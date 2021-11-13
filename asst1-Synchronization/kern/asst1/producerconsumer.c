/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include "producerconsumer.h"

/* Declare any variables you need here to keep track of and
   synchronise your bounded buffer. A sample declaration of a buffer is shown
   below. It is an array of pointers to items.

   You can change this if you choose another implementation.
   However, your implementation should accept at least BUFFER_SIZE
   prior to blocking
*/

#define BUFFLEN (BUFFER_SIZE + 1)

data_item_t * item_buffer[BUFFER_SIZE+1];
struct cv *empty;
struct cv *full;
struct lock *new_lock;

volatile int head, tail;


/* consumer_receive() is called by a consumer to request more data. It
   should block on a sync primitive if no data is available in your
   buffer. It should not busy wait! */

data_item_t * consumer_receive(void)
{
        data_item_t * item;
        lock_acquire(new_lock);
        while(head == tail)
                cv_wait(empty, new_lock);
        item = item_buffer[tail];
        tail = (tail + 1) % BUFFLEN;
        cv_signal(full, new_lock);
        lock_release(new_lock);
        /******************
         * Remove above here
         */

        return item;
}

/* procucer_send() is called by a producer to store data in your
   bounded buffer.  It should block on a sync primitive if no space is
   available in your buffer. It should not busy wait!*/

void producer_send(data_item_t *item)
{
        lock_acquire(new_lock);
        while((head + 1) % BUFFLEN == tail) {
                cv_wait(full,new_lock);
        }
        item_buffer[head] = item;
        head = (head + 1) % BUFFLEN;
        cv_signal(empty, new_lock);
        lock_release(new_lock);
}




/* Perform any initialisation (e.g. of global data) you need
   here. Note: You can panic if any allocation fails during setup */

void producerconsumer_startup(void)
{
        new_lock = lock_create("new_lock");
        full = cv_create("full");
        empty = cv_create("empty");
        if (new_lock == NULL || full == NULL || empty == NULL)
                panic("Initialisation fails");
        head = tail = 0;

}

/* Perform any clean-up you need here */
void producerconsumer_shutdown(void)
{
        lock_destroy(new_lock);
        cv_destroy(full);
        cv_destroy(empty);
}

