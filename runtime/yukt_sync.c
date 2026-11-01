#include "yukt_runtime.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct YuktChanMsg {
    struct YuktChanMsg* next;
    void (*drop)(void*);
    int64_t nbytes;
    char payload[];
} YuktChanMsg;

typedef struct YuktChanQ {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    YuktChanMsg* head;
    YuktChanMsg* tail;
    int send_closed;
    int recv_closed;
    int refs;
    int receiver_taken;
} YuktChanQ;

typedef struct YuktSend {
    int tag;
    YuktChanQ* q;
} YuktSend;

typedef struct YuktRecv {
    int tag;
    YuktChanQ* q;
} YuktRecv;

static _Thread_local YuktChanMsg* yukt_sync_stash = NULL;

static void yukt_sync_drop_msg(YuktChanMsg* msg) {
    if (msg->drop != NULL) {
        msg->drop(msg->payload);
    }
    free(msg);
}

void yukt_sync_drop_ptr(void* payload) {
    void* owned = NULL;
    if (payload != NULL) {
        memcpy(&owned, payload, sizeof(owned));
    }
    yukt_release(owned);
}

static void yukt_sync_drop_queued(YuktChanQ* q) {
    YuktChanMsg* msg = q->head;
    q->head = NULL;
    q->tail = NULL;
    while (msg != NULL) {
        YuktChanMsg* next = msg->next;
        yukt_sync_drop_msg(msg);
        msg = next;
    }
}

static void yukt_sync_close_q(YuktChanQ* q, int is_send) {
    if (is_send) {
        if (q->send_closed) {
            return;
        }
        q->send_closed = 1;
        pthread_cond_broadcast(&q->cv);
        return;
    }
    if (q->recv_closed) {
        return;
    }
    q->recv_closed = 1;
    yukt_sync_drop_queued(q);
    pthread_cond_broadcast(&q->cv);
}

static YuktChanQ* yukt_sync_end_queue(void* end, int* is_send) {
    if (end == NULL) {
        return NULL;
    }
    const int tag = *(int*)end;
    if (tag == YUKT_ALLOC_SEND) {
        if (is_send != NULL) {
            *is_send = 1;
        }
        return ((YuktSend*)end)->q;
    }
    if (tag == YUKT_ALLOC_RECV) {
        if (is_send != NULL) {
            *is_send = 0;
        }
        return ((YuktRecv*)end)->q;
    }
    return NULL;
}

void* yukt_sync_channel(void) {
    YuktSend* send = (YuktSend*)calloc(1, sizeof(YuktSend));
    YuktChanQ* q = (YuktChanQ*)calloc(1, sizeof(YuktChanQ));
    if (send == NULL || q == NULL) {
        free(send);
        free(q);
        die("out of memory");
    }
    if (pthread_mutex_init(&q->mu, NULL) != 0 || pthread_cond_init(&q->cv, NULL) != 0) {
        free(send);
        free(q);
        die("out of memory");
    }
    q->refs = 1;
    q->receiver_taken = 0;
    send->tag = YUKT_ALLOC_SEND;
    send->q = q;
    yukt_track(YUKT_ALLOC_SEND, send, sizeof(YuktSend));
    return send;
}

void* yukt_sync_receiver(void* tx) {
    YuktSend* send = (YuktSend*)tx;
    YuktChanQ* q = send != NULL ? send->q : NULL;
    if (q == NULL) {
        die("receiver already taken");
    }
    pthread_mutex_lock(&q->mu);
    if (q->receiver_taken) {
        pthread_mutex_unlock(&q->mu);
        die("receiver already taken");
    }
    q->receiver_taken = 1;
    q->refs++;
    pthread_mutex_unlock(&q->mu);
    YuktRecv* recv = (YuktRecv*)calloc(1, sizeof(YuktRecv));
    if (recv == NULL) {
        die("out of memory");
    }
    recv->tag = YUKT_ALLOC_RECV;
    recv->q = q;
    yukt_track(YUKT_ALLOC_RECV, recv, sizeof(YuktRecv));
    return recv;
}

void yukt_sync_send(void* tx, const void* payload, int64_t nbytes, void (*drop)(void*)) {
    YuktSend* send = (YuktSend*)tx;
    YuktChanQ* q = send != NULL ? send->q : NULL;
    if (q == NULL) {
        die("send on closed channel");
    }
    pthread_mutex_lock(&q->mu);
    if (q->send_closed || q->recv_closed) {
        pthread_mutex_unlock(&q->mu);
        die("send on closed channel");
    }
    const size_t extra = nbytes > 0 ? (size_t)nbytes : 0;
    YuktChanMsg* msg = (YuktChanMsg*)malloc(sizeof(YuktChanMsg) + extra);
    if (msg == NULL) {
        pthread_mutex_unlock(&q->mu);
        die("out of memory");
    }
    msg->next = NULL;
    msg->drop = drop;
    msg->nbytes = nbytes > 0 ? nbytes : 0;
    if (extra > 0 && payload != NULL) {
        memcpy(msg->payload, payload, extra);
    }
    if (q->tail == NULL) {
        q->head = msg;
        q->tail = msg;
    } else {
        q->tail->next = msg;
        q->tail = msg;
    }
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

int64_t yukt_sync_recv_status(void* rx) {
    YuktRecv* recv = (YuktRecv*)rx;
    YuktChanQ* q = recv != NULL ? recv->q : NULL;
    if (q == NULL) {
        return 1;
    }
    pthread_mutex_lock(&q->mu);
    while (q->head == NULL && !q->send_closed && !q->recv_closed) {
        pthread_cond_wait(&q->cv, &q->mu);
    }
    if (q->head != NULL) {
        YuktChanMsg* msg = q->head;
        q->head = msg->next;
        if (q->head == NULL) {
            q->tail = NULL;
        }
        pthread_mutex_unlock(&q->mu);
        yukt_sync_stash = msg;
        return 0;
    }
    pthread_mutex_unlock(&q->mu);
    return 1;
}

void yukt_sync_recv_unchecked(void* out, int64_t nbytes) {
    YuktChanMsg* msg = yukt_sync_stash;
    yukt_sync_stash = NULL;
    if (msg == NULL) {
        return;
    }
    if (out != NULL && nbytes > 0) {
        memcpy(out, msg->payload, (size_t)nbytes);
    }
    free(msg);
}

void yukt_sync_close(void* end) {
    int is_send = 0;
    YuktChanQ* q = yukt_sync_end_queue(end, &is_send);
    if (q == NULL) {
        return;
    }
    pthread_mutex_lock(&q->mu);
    yukt_sync_close_q(q, is_send);
    pthread_mutex_unlock(&q->mu);
}

void yukt_sync_destroy_end(void* ptr) {
    int is_send = 0;
    YuktChanQ* q = yukt_sync_end_queue(ptr, &is_send);
    if (q == NULL) {
        return;
    }
    if (is_send) {
        ((YuktSend*)ptr)->q = NULL;
    } else {
        ((YuktRecv*)ptr)->q = NULL;
    }
    pthread_mutex_lock(&q->mu);
    yukt_sync_close_q(q, is_send);
    q->refs--;
    const int last = q->refs <= 0;
    pthread_mutex_unlock(&q->mu);
    if (!last) {
        return;
    }
    yukt_sync_drop_queued(q);
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->cv);
    free(q);
}
