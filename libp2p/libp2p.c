/*****************************************************************************
 * Copyright (C) 2014-2015
 * file:    libp2p.c
 * author:  gozfree <gozfree@163.com>
 * created: 2015-05-20 00:01
 * updated: 2015-05-20 00:01
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#include <libgzf.h>
#include <libstun.h>
#include <libskt.h>
#include <librpc.h>
#include <libptcp.h>
#include "libp2p.h"

static short _rpc_port = 12345;
static char _local_ip[INET_ADDRSTRLEN];
static short _local_port = 0;
static struct p2p *_p2p = NULL;
int _p2p_connect(struct p2p *p2p, char *ip, short port);
void *tmp_thread(void *arg);

static int on_get_connect_list_resp(struct rpc *r, void *arg, int len)
{
    void *ptr;
    int num = 0;
    //logi("on_get_connect_list, len = %d\n", len);
    num = len / MAX_UUID_LEN;
    for (ptr = arg; num > 0; --num) {
        printf("uuid list: %p\n", ptr);
        len = MAX_UUID_LEN;
        ptr += len;
    }
    return 0;
}

static int on_test_resp(struct rpc *r, void *arg, int len)
{
    printf("on_test_resp\n");
    return 0;
}

static int on_peer_post_msg_resp(struct rpc *r, void *arg, int len)
{
    pthread_t tid;
    struct p2p *p2p = _p2p;
    char *peer_id;
    char localip[MAX_ADDR_STRING];
    char reflectip[MAX_ADDR_STRING];
    struct sockaddr_in si;
    printf("on_peer_post_msg_resp len = %d\n", len);
    struct nat_info *nat = (struct nat_info *)arg;
    printf("peer info\n");
    printf("nat.uuid = %s\n", nat->uuid);
    skt_addr_ntop(localip, nat->local.ip);
    skt_addr_ntop(reflectip, nat->reflect.ip);
    printf("nat.type = %d, local.ip = %s, port = %d\n",
            nat->type, localip, nat->local.port);
    printf("reflect.ip = %s, port = %d\n",
            reflectip, nat->reflect.port);
    p2p->ps = ptcp_socket_by_fd(p2p->nat.fd);
    if (p2p->ps == NULL) {
        printf("error!\n");
        return -1;
    }
    if (p2p->rpc_state == P2P_RPC_SYN_SENT) {//client
        sleep(1);
        _p2p_connect(p2p, reflectip, nat->reflect.port);
        return -1;
    }
//server
    peer_id = nat->uuid;
    p2p_connect(p2p, peer_id);

    si.sin_family = AF_INET;
    si.sin_addr.s_addr = inet_addr(_local_ip);
    si.sin_port = htons(_local_port);

    printf("ptcp_bind %s:%d\n", _local_ip, _local_port);
    ptcp_bind(p2p->ps, (struct sockaddr*)&si, sizeof(si));
    ptcp_listen(p2p->ps, 0);
    pthread_create(&tid, NULL, tmp_thread, p2p);
    return 0;
}

BEGIN_MSG_MAP(BASIC_RPC_API_RESP)
MSG_ACTION(RPC_TEST, on_test_resp)
MSG_ACTION(RPC_GET_CONNECT_LIST, on_get_connect_list_resp)
MSG_ACTION(RPC_PEER_POST_MSG, on_peer_post_msg_resp)
END_MSG_MAP()

int rpc_get_connect_list(struct rpc *r)
{
    int len = 100;
    char *buf = calloc(1, len);
    memset(buf, 0xA5, len);
    rpc_call(r, RPC_GET_CONNECT_LIST, buf, len, NULL, 0);
    //printf("func_id = %x\n", RPC_GET_CONNECT_LIST);
    //dump_packet(&r->packet);
    return 0;
}

void p2p_get_peer_list(struct p2p *p2p)
{
    rpc_get_connect_list(p2p->rpc);
}

int rpc_peer_post_msg(struct rpc *r, void *buf, size_t len)
{
    rpc_call(r, RPC_PEER_POST_MSG, buf, len, NULL, 0);
    //printf("func_id = %x\n", RPC_PEER_POST_MSG);
    //dump_packet(&r->packet);
    return 0;
}


int p2p_send(struct p2p *p2p, void *buf, int len)
{
    return ptcp_send(p2p->ps, buf, len);
}

int p2p_recv(struct p2p *p2p, void *buf, int len)
{
    return ptcp_recv(p2p->ps, buf, len);
}

int _p2p_connect(struct p2p *p2p, char *ip, short port)
{
    struct sockaddr_in si;
    si.sin_family = AF_INET;
    si.sin_addr.s_addr = inet_addr(ip);
    si.sin_port = htons(port);
    printf("ptcp_connect %s:%d\n", ip, port);
    if (0 != ptcp_connect(p2p->ps, (struct sockaddr*)&si, sizeof(si))) {
        return -1;
    } else {
        printf("ptcp_connect success\n");
    }
    return 0;
}

void *tmp_thread(void *arg)
{
    struct p2p *p2p = (struct p2p *)arg;
    int len;
    char buf[32] = {0};
    while (1) {
        memset(buf, 0, sizeof(buf));
        len = ptcp_recv(p2p->ps, buf, sizeof(buf));
        if (len > 0) {
            printf("ptcp_recv len=%d, buf=%s\n", len, buf);
        } else if (ptcp_is_closed(p2p->ps)) {
            printf("ptcp is closed\n");
        } else if (EWOULDBLOCK == ptcp_get_error(p2p->ps)){
            //printf("ptcp is error: %d\n", ptcp_get_error(p2p->ps));
            usleep(100 * 1000);
        }
    }
    return NULL;
}

void on_rpc_read(int fd, void *arg)
{
    struct p2p *p2p = (struct p2p *)arg;
    struct rpc *r = p2p->rpc;
    struct iobuf *buf = rpc_recv_buf(r);
    if (!buf) {
        printf("rpc_recv_buf failed!\n");
        return;
    }
    process_msg2(r, buf);
}

#if 0
void on_rpc_read(int fd, void *arg)
{
    pthread_t tid;
    struct p2p *p2p = (struct p2p *)arg;
    struct rpc *r = p2p->rpc;
    char *peer_id;
    char localip[MAX_ADDR_STRING];
    char reflectip[MAX_ADDR_STRING];
    struct sockaddr_in si;
    struct iobuf *buf = rpc_recv_buf(r);
    if (!buf) {
        printf("rpc_recv_buf failed!\n");
        return;
    }
    struct nat_info *nat = (struct nat_info *)buf->addr;
    printf("peer info\n");
    printf("nat.uuid = %s\n", nat->uuid);
    skt_addr_ntop(localip, nat->local.ip);
    skt_addr_ntop(reflectip, nat->reflect.ip);
    printf("nat.type = %d, local.ip = %s, port = %d\n",
            nat->type, localip, nat->local.port);
    printf("reflect.ip = %s, port = %d\n",
            reflectip, nat->reflect.port);
    p2p->ps = ptcp_socket_by_fd(p2p->nat.fd);
    if (p2p->ps == NULL) {
        printf("error!\n");
        return;
    }
    if (p2p->rpc_state == P2P_RPC_SYN_SENT) {//client
        sleep(1);
        _p2p_connect(p2p, reflectip, nat->reflect.port);
        return;
    }
//server
    peer_id = nat->uuid;
    p2p_connect(p2p, peer_id);

    si.sin_family = AF_INET;
    si.sin_addr.s_addr = inet_addr(_local_ip);
    si.sin_port = htons(_local_port);

    printf("ptcp_bind %s:%d\n", _local_ip, _local_port);
    ptcp_bind(p2p->ps, (struct sockaddr*)&si, sizeof(si));
    ptcp_listen(p2p->ps, 0);
    pthread_create(&tid, NULL, tmp_thread, p2p);
}
#endif

void on_rpc_write(int fd, void *arg)
{
    printf("on_write fd= %d\n", fd);
}

void on_rpc_error(int fd, void *arg)
{
    printf("on_error fd= %d\n", fd);
}

int rand()
{
    uint64_t tick;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tick = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);

    int seed = (int)tick;
    srandom(seed);
    return random();
}

int random_port()
{
    int min = 0x4000;
    int max = 0x7FFF;

    int ret = rand();
    ret = ret | min;
    ret = ret & max;

    return ret;
}
struct p2p *p2p_init(const char *rpc_srv, const char *stun_srv)
{
    char ip[64];
    struct skt_addr tmpaddr;
    static stun_addr _mapped;
    struct p2p *p2p = CALLOC(1, struct p2p);
    if (!p2p) {
        printf("malloc failed: %d\n", errno);
        return NULL;
    }

    p2p->rpc = rpc_create(rpc_srv, _rpc_port);
    if (!p2p->rpc) {
        printf("rpc_create failed\n");
        return NULL;
    }
    REGISTER_MSG_MAP(BASIC_RPC_API_RESP);
    rpc_set_cb(p2p->rpc, on_rpc_read, on_rpc_write, on_rpc_error, p2p);
    skt_getaddr_by_fd(p2p->rpc->fd, &tmpaddr);
    skt_addr_ntop(_local_ip, tmpaddr.ip);
    _local_port = tmpaddr.port;

    stun_init(stun_srv);
    p2p->nat.type = stun_nat_type();
    strncpy(p2p->nat.uuid, p2p->rpc->packet.header.uuid_src, MAX_UUID_LEN);
    p2p->nat.local.ip = skt_addr_pton(_local_ip);

    _local_port = random_port();
    p2p->nat.local.port = _local_port;
    p2p->nat.fd = stun_socket(_local_ip, _local_port, &_mapped);
    _mapped.addr = ntohl(_mapped.addr);
    skt_addr_ntop(ip, _mapped.addr);
    p2p->nat.reflect.ip = _mapped.addr;
    p2p->nat.reflect.port = _mapped.port;
    printf("nat.type = %d, local: %s:%d\n",
            p2p->nat.type, _local_ip, p2p->nat.local.port);
    printf("reflect: %s:%d\n",
            ip, p2p->nat.reflect.port);
    p2p->rpc_state = P2P_RPC_INIT;
    _p2p = p2p;
    return p2p;
}

int p2p_connect(struct p2p *p2p, char *peer_id)
{
    int len = (int)sizeof(struct nat_info);
    strncpy(p2p->rpc->packet.header.uuid_dst, peer_id, MAX_UUID_LEN);
    //rpc_send(p2p->rpc, (void *)&p2p->nat, len);
    rpc_peer_post_msg(p2p->rpc, (void *)&p2p->nat, len);
    p2p->rpc_state = P2P_RPC_SYN_SENT;

    return 0;
}

int p2p_dispatch(struct p2p *p2p)
{
    return rpc_dispatch(p2p->rpc);
}

void p2p_deinit(struct p2p *p)
{

}