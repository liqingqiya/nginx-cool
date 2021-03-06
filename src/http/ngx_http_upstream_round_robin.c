
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static ngx_http_upstream_rr_peer_t *ngx_http_upstream_get_peer(
    ngx_http_upstream_rr_peer_data_t *rrp);

#if (NGX_HTTP_SSL)

static ngx_int_t ngx_http_upstream_empty_set_session(ngx_peer_connection_t *pc,
    void *data);
static void ngx_http_upstream_empty_save_session(ngx_peer_connection_t *pc,
    void *data);

#endif


ngx_int_t
ngx_http_upstream_init_round_robin(ngx_conf_t *cf,              /*round robin init*/
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_url_t                      u;
    ngx_uint_t                     i, j, n, w;
    ngx_http_upstream_server_t    *server;
    ngx_http_upstream_rr_peers_t  *peers, *backup;

    us->peer.init = ngx_http_upstream_init_round_robin_peer;    /*设置初始化服务器的回调函数*/

    if (us->servers) {
        server = us->servers->elts;

        n = 0;
        w = 0;

        for (i = 0; i < us->servers->nelts; i++) {
            if (server[i].backup) {                                 /*如果是后备服务器，那么就跳过*/
                continue;
            }

            n += server[i].naddrs;
            w += server[i].naddrs * server[i].weight;
        }

        if (n == 0) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "no servers in upstream \"%V\" in %s:%ui",
                          &us->host, us->file_name, us->line);
            return NGX_ERROR;
        }
        /*分配上游服务器列表的内存*/
        peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_rr_peers_t)
                              + sizeof(ngx_http_upstream_rr_peer_t) * (n - 1));  /*peers代表上游服务器*/
        if (peers == NULL) {
            return NGX_ERROR;
        }
        /*初始化上游服务器列表*/
        peers->single = (n == 1);       /*是否是单机模式*/
        peers->number = n;              /*服务器的数量*/
        peers->weighted = (w != n);     /*当所有的权重为1的时候，weighted为0,否则为1,也就是是否设置了权重的一个标记*/
        peers->total_weight = w;        /*所有权重之和*/
        peers->name = &us->host;        /*服务器名字为解析之后的主机名*/

        n = 0;

        for (i = 0; i < us->servers->nelts; i++) {
            if (server[i].backup) {
                continue;
            }
            /*解析之后的配置存在server里？todo*/
            /*初始化我们每一个非后备服务器*/
            for (j = 0; j < server[i].naddrs; j++) {
                peers->peer[n].sockaddr = server[i].addrs[j].sockaddr;
                peers->peer[n].socklen = server[i].addrs[j].socklen;
                peers->peer[n].name = server[i].addrs[j].name;
                peers->peer[n].weight = server[i].weight;
                peers->peer[n].effective_weight = server[i].weight;
                peers->peer[n].current_weight = 0;
                peers->peer[n].max_fails = server[i].max_fails;
                peers->peer[n].fail_timeout = server[i].fail_timeout;
                peers->peer[n].down = server[i].down;
                n++;
            }
        }

        us->peer.data = peers;                              /*初始化上游服务器列表之后，挂载非后备服务器列表*/

        /* backup servers */

        n = 0;
        w = 0;

        for (i = 0; i < us->servers->nelts; i++) {          /*处理后备服务器列表*/
            if (!server[i].backup) {
                continue;
            }

            n += server[i].naddrs;                            /*所有域名对应的所有ip地址个数*/
            w += server[i].naddrs * server[i].weight;       /*后备服务器的总权重*/
        }

        if (n == 0) {
            return NGX_OK;                                      /*没有配置后备服务器，结束初始化*/
        }

        backup = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_rr_peers_t)
                              + sizeof(ngx_http_upstream_rr_peer_t) * (n - 1));
        if (backup == NULL) {
            return NGX_ERROR;
        }
        /*初始化backup链表，也即是后备链表*/
        peers->single = 0;
        backup->single = 0;
        backup->number = n;
        backup->weighted = (w != n);
        backup->total_weight = w;
        backup->name = &us->host;

        n = 0;
        /*初始化后备链表的每一个服务器*/
        for (i = 0; i < us->servers->nelts; i++) {
            if (!server[i].backup) {
                continue;
            }

            for (j = 0; j < server[i].naddrs; j++) {
                backup->peer[n].sockaddr = server[i].addrs[j].sockaddr;
                backup->peer[n].socklen = server[i].addrs[j].socklen;
                backup->peer[n].name = server[i].addrs[j].name;
                backup->peer[n].weight = server[i].weight;
                backup->peer[n].effective_weight = server[i].weight;
                backup->peer[n].current_weight = 0;
                backup->peer[n].max_fails = server[i].max_fails;
                backup->peer[n].fail_timeout = server[i].fail_timeout;
                backup->peer[n].down = server[i].down;
                n++;
            }
        }
        /*初始化之后挂载到peers->next字段上，挂载后备服务器列表*/
        peers->next = backup;

        return NGX_OK;
    }


    /* an upstream implicitly defined by proxy_pass, etc. */

    if (us->port == 0) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "no port in upstream \"%V\" in %s:%ui",
                      &us->host, us->file_name, us->line);
        return NGX_ERROR;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.host = us->host;
    u.port = us->port;

    if (ngx_inet_resolve_host(cf->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "%s in upstream \"%V\" in %s:%ui",
                          u.err, &us->host, us->file_name, us->line);
        }

        return NGX_ERROR;
    }

    n = u.naddrs;

    peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_rr_peers_t)
                              + sizeof(ngx_http_upstream_rr_peer_t) * (n - 1));
    if (peers == NULL) {
        return NGX_ERROR;
    }

    peers->single = (n == 1);
    peers->number = n;
    peers->weighted = 0;
    peers->total_weight = n;
    peers->name = &us->host;

    for (i = 0; i < u.naddrs; i++) {
        peers->peer[i].sockaddr = u.addrs[i].sockaddr;
        peers->peer[i].socklen = u.addrs[i].socklen;
        peers->peer[i].name = u.addrs[i].name;
        peers->peer[i].weight = 1;
        peers->peer[i].effective_weight = 1;
        peers->peer[i].current_weight = 0;
        peers->peer[i].max_fails = 1;
        peers->peer[i].fail_timeout = 10;
    }

    us->peer.data = peers;

    /* implicitly defined upstream has no backup servers */

    return NGX_OK;
}


ngx_int_t
ngx_http_upstream_init_round_robin_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)                                   /*该函数在针对`每个请求`选择`后端服务器`前被调用*/
{
    ngx_uint_t                         n;
    ngx_http_upstream_rr_peer_data_t  *rrp;

    rrp = r->upstream->peer.data;

    if (rrp == NULL) {
        rrp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_rr_peer_data_t));
        if (rrp == NULL) {
            return NGX_ERROR;
        }

        r->upstream->peer.data = rrp;                   /*rrp???todo*/
    }

    rrp->peers = us->peer.data;
    rrp->current = 0;

    n = rrp->peers->number;                             /*上游服务器的个数*/

    if (rrp->peers->next && rrp->peers->next->number > n) {
        n = rrp->peers->next->number;
    }

    if (n <= 8 * sizeof(uintptr_t)) {
        rrp->tried = &rrp->data;
        rrp->data = 0;

    } else {
        n = (n + (8 * sizeof(uintptr_t) - 1)) / (8 * sizeof(uintptr_t));

        rrp->tried = ngx_pcalloc(r->pool, n * sizeof(uintptr_t));
        if (rrp->tried == NULL) {
            return NGX_ERROR;
        }
    }

    r->upstream->peer.get = ngx_http_upstream_get_round_robin_peer;     /*设置了两个回调函数*/
    r->upstream->peer.free = ngx_http_upstream_free_round_robin_peer;   /*todo*/
    r->upstream->peer.tries = rrp->peers->number;                          /*todo*/
#if (NGX_HTTP_SSL)
    r->upstream->peer.set_session =
                               ngx_http_upstream_set_round_robin_peer_session;
    r->upstream->peer.save_session =
                               ngx_http_upstream_save_round_robin_peer_session;
#endif

    return NGX_OK;
}


ngx_int_t
ngx_http_upstream_create_round_robin_peer(ngx_http_request_t *r,
    ngx_http_upstream_resolved_t *ur)
{
    u_char                            *p;
    size_t                             len;
    socklen_t                          socklen;
    ngx_uint_t                         i, n;
    struct sockaddr                   *sockaddr;
    ngx_http_upstream_rr_peers_t      *peers;
    ngx_http_upstream_rr_peer_data_t  *rrp;

    rrp = r->upstream->peer.data;

    if (rrp == NULL) {
        rrp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_rr_peer_data_t));
        if (rrp == NULL) {
            return NGX_ERROR;
        }

        r->upstream->peer.data = rrp;
    }

    peers = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_rr_peers_t)
                     + sizeof(ngx_http_upstream_rr_peer_t) * (ur->naddrs - 1));
    if (peers == NULL) {
        return NGX_ERROR;
    }

    peers->single = (ur->naddrs == 1);
    peers->number = ur->naddrs;
    peers->name = &ur->host;

    if (ur->sockaddr) {
        peers->peer[0].sockaddr = ur->sockaddr;
        peers->peer[0].socklen = ur->socklen;
        peers->peer[0].name = ur->host;
        peers->peer[0].weight = 1;
        peers->peer[0].effective_weight = 1;
        peers->peer[0].current_weight = 0;
        peers->peer[0].max_fails = 1;
        peers->peer[0].fail_timeout = 10;

    } else {

        for (i = 0; i < ur->naddrs; i++) {

            socklen = ur->addrs[i].socklen;

            sockaddr = ngx_palloc(r->pool, socklen);
            if (sockaddr == NULL) {
                return NGX_ERROR;
            }

            ngx_memcpy(sockaddr, ur->addrs[i].sockaddr, socklen);

            switch (sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
            case AF_INET6:
                ((struct sockaddr_in6 *) sockaddr)->sin6_port = htons(ur->port);
                break;
#endif
            default: /* AF_INET */
                ((struct sockaddr_in *) sockaddr)->sin_port = htons(ur->port);
            }

            p = ngx_pnalloc(r->pool, NGX_SOCKADDR_STRLEN);
            if (p == NULL) {
                return NGX_ERROR;
            }

            len = ngx_sock_ntop(sockaddr, socklen, p, NGX_SOCKADDR_STRLEN, 1);

            peers->peer[i].sockaddr = sockaddr;
            peers->peer[i].socklen = socklen;
            peers->peer[i].name.len = len;
            peers->peer[i].name.data = p;
            peers->peer[i].weight = 1;
            peers->peer[i].effective_weight = 1;
            peers->peer[i].current_weight = 0;
            peers->peer[i].max_fails = 1;
            peers->peer[i].fail_timeout = 10;
        }
    }

    rrp->peers = peers;
    rrp->current = 0;

    if (rrp->peers->number <= 8 * sizeof(uintptr_t)) {
        rrp->tried = &rrp->data;
        rrp->data = 0;

    } else {
        n = (rrp->peers->number + (8 * sizeof(uintptr_t) - 1))
                / (8 * sizeof(uintptr_t));

        rrp->tried = ngx_pcalloc(r->pool, n * sizeof(uintptr_t));
        if (rrp->tried == NULL) {
            return NGX_ERROR;
        }
    }

    r->upstream->peer.get = ngx_http_upstream_get_round_robin_peer;
    r->upstream->peer.free = ngx_http_upstream_free_round_robin_peer;
    r->upstream->peer.tries = rrp->peers->number;
#if (NGX_HTTP_SSL)
    r->upstream->peer.set_session = ngx_http_upstream_empty_set_session;
    r->upstream->peer.save_session = ngx_http_upstream_empty_save_session;
#endif

    return NGX_OK;
}


ngx_int_t
ngx_http_upstream_get_round_robin_peer(ngx_peer_connection_t *pc, void *data)  /*轮询选择后端服务器*/
{
    ngx_http_upstream_rr_peer_data_t  *rrp = data;                  /*rrp？？代表什么？？todo*/

    ngx_int_t                      rc;
    ngx_uint_t                     i, n;
    ngx_http_upstream_rr_peer_t   *peer;
    ngx_http_upstream_rr_peers_t  *peers;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "get rr peer, try: %ui", pc->tries);                             /*日志打印*/

    /* ngx_lock_mutex(rrp->peers->mutex); */

    pc->cached = 0;
    pc->connection = NULL;

    if (rrp->peers->single) {                                   /*判断时候只有一台服务器*/
        peer = &rrp->peers->peer[0];                            /*yes , 选择他*/

        if (peer->down) {                                        /*只有一台，却是宕机，则选择失败*/
            goto failed;
        }

    } else {

        /*有多台服务器可用*/

        peer = ngx_http_upstream_get_peer(rrp);              /*选择权值最高的一台服务器*/

        if (peer == NULL) {                                     /*选择失败*/
            goto failed;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "get rr peer, current: %ui %i",
                       rrp->current, peer->current_weight);  /*打印日志信息,定义在/src/core/ngx_log.h模块中*/
    }

    pc->sockaddr = peer->sockaddr;                            /*赋值当前选到的服务器信息*/
    pc->socklen = peer->socklen;
    pc->name = &peer->name;

    /* ngx_unlock_mutex(rrp->peers->mutex); */

    if (pc->tries == 1 && rrp->peers->next) {
        pc->tries += rrp->peers->next->number;
    }

    return NGX_OK;

failed:

    peers = rrp->peers;

    if (peers->next) {

        /* ngx_unlock_mutex(peers->mutex); */

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, pc->log, 0, "backup servers");

        rrp->peers = peers->next;
        pc->tries = rrp->peers->number;

        n = (rrp->peers->number + (8 * sizeof(uintptr_t) - 1))
                / (8 * sizeof(uintptr_t));

        for (i = 0; i < n; i++) {
             rrp->tried[i] = 0;
        }

        rc = ngx_http_upstream_get_round_robin_peer(pc, rrp);

        if (rc != NGX_BUSY) {
            return rc;
        }

        /* ngx_lock_mutex(peers->mutex); */
    }

    /* all peers failed, mark them as live for quick recovery */

    for (i = 0; i < peers->number; i++) {
        peers->peer[i].fails = 0;
    }

    /* ngx_unlock_mutex(peers->mutex); */

    pc->name = peers->name;

    return NGX_BUSY;
}


static ngx_http_upstream_rr_peer_t *
ngx_http_upstream_get_peer(ngx_http_upstream_rr_peer_data_t *rrp)   /*rrp??todo*/
{
    time_t                        now;
    uintptr_t                     m;
    ngx_int_t                     total;
    ngx_uint_t                    i, n;
    ngx_http_upstream_rr_peer_t  *peer, *best;

    now = ngx_time();

    best = NULL;        /* 存储选出的服务器 */
    total = 0;

    for (i = 0; i < rrp->peers->number; i++) {      /*遍历上游服务器*/

        n = i / (8 * sizeof(uintptr_t));
        m = (uintptr_t) 1 << i % (8 * sizeof(uintptr_t));       /*m相当于掩码，用于计算是否被选择且失败过*/

        if (rrp->tried[n] & m) {    /* 已经选择过了, tried是一个位图，用于存储这些信息 */
            continue;
        }

        peer = &rrp->peers->peer[i];

        if (peer->down) {           /*宕机，那么就不考虑该服务器，跳过本次循环*/
            continue;
        }

        if (peer->max_fails        /*连接失败次数达到最大，并且还在fail_timeout时间内，则跳过本次循环*/
            && peer->fails >= peer->max_fails
            && now - peer->checked <= peer->fail_timeout)   /*checked存储本轮轮询时间*/
        {
            continue;
        }

        peer->current_weight += peer->effective_weight;     /* effective_weight是代表什么？current_weight呢？ */
        total += peer->effective_weight;

        if (peer->effective_weight < peer->weight) {        /*effective_weight应该不会降低到比weight还低啊？为什么还要这样比较？todo*/
            peer->effective_weight++;                           /*初始设定的effective_weight降低到比weight还低的时候，effective_weight++*/
        }

        if (best == NULL || peer->current_weight > best->current_weight) {  /* 选出的指标是current_weight,那么effective_weight有什么作用？ */
            best = peer;
        }
    }

    if (best == NULL) {
        return NULL;
    }

    i = best - &rrp->peers->peer[0];            /*该服务器索引*/

    rrp->current = i;                            /*该服务器索引*/

    n = i / (8 * sizeof(uintptr_t));
    m = (uintptr_t) 1 << i % (8 * sizeof(uintptr_t));       /*m为什么是这么计算的？？*/

    rrp->tried[n] |= m;                         /*位图存储该轮被选择的服务器*/

    best->current_weight -= total;            /*选中的降低权值*/

    if (now - best->checked > best->fail_timeout) {
        best->checked = now;                  /*如果时间超出了fail_timeout，则重置checked时间*/
    }

    return best;                              /*返回权值最大的，最佳的服务器*/
}


void
ngx_http_upstream_free_round_robin_peer(ngx_peer_connection_t *pc, void *data,
    ngx_uint_t state)                                               /*请求处理完之后，释放后端服务器*/
{
    ngx_http_upstream_rr_peer_data_t  *rrp = data;

    time_t                       now;
    ngx_http_upstream_rr_peer_t  *peer;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "free rr peer %ui %ui", pc->tries, state);

    /* TODO: NGX_PEER_KEEPALIVE */

    if (rrp->peers->single) {           /*一台服务器*/
        pc->tries = 0;                   /*这就释放了？？？*/
        return;
    }

    peer = &rrp->peers->peer[rrp->current];

    if (state & NGX_PEER_FAILED) {     /*失败的状态，那么就要更新fails变量*/
        now = ngx_time();

        /* ngx_lock_mutex(rrp->peers->mutex); */

        peer->fails++;
        peer->accessed = now;
        peer->checked = now;

        if (peer->max_fails) {
            peer->effective_weight -= peer->weight / peer->max_fails;       /*这里是对512行代码的解释，懂了*/
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "free rr peer failed: %ui %i",
                       rrp->current, peer->effective_weight);

        if (peer->effective_weight < 0) {
            peer->effective_weight = 0;
        }

        /* ngx_unlock_mutex(rrp->peers->mutex); */

    } else {

        /* mark peer live if check passed */

        if (peer->accessed < peer->checked) {
            peer->fails = 0;
        }
    }

    if (pc->tries) {
        pc->tries--;
    }

    /* ngx_unlock_mutex(rrp->peers->mutex); */
}


#if (NGX_HTTP_SSL)

ngx_int_t
ngx_http_upstream_set_round_robin_peer_session(ngx_peer_connection_t *pc,
    void *data)
{
    ngx_http_upstream_rr_peer_data_t  *rrp = data;

    ngx_int_t                     rc;
    ngx_ssl_session_t            *ssl_session;
    ngx_http_upstream_rr_peer_t  *peer;

    peer = &rrp->peers->peer[rrp->current];

    /* TODO: threads only mutex */
    /* ngx_lock_mutex(rrp->peers->mutex); */

    ssl_session = peer->ssl_session;

    rc = ngx_ssl_set_session(pc->connection, ssl_session);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "set session: %p:%d",
                   ssl_session, ssl_session ? ssl_session->references : 0);

    /* ngx_unlock_mutex(rrp->peers->mutex); */

    return rc;
}


void
ngx_http_upstream_save_round_robin_peer_session(ngx_peer_connection_t *pc,
    void *data)
{
    ngx_http_upstream_rr_peer_data_t  *rrp = data;

    ngx_ssl_session_t            *old_ssl_session, *ssl_session;
    ngx_http_upstream_rr_peer_t  *peer;

    ssl_session = ngx_ssl_get_session(pc->connection);

    if (ssl_session == NULL) {
        return;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "save session: %p:%d", ssl_session, ssl_session->references);

    peer = &rrp->peers->peer[rrp->current];

    /* TODO: threads only mutex */
    /* ngx_lock_mutex(rrp->peers->mutex); */

    old_ssl_session = peer->ssl_session;
    peer->ssl_session = ssl_session;

    /* ngx_unlock_mutex(rrp->peers->mutex); */

    if (old_ssl_session) {

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "old session: %p:%d",
                       old_ssl_session, old_ssl_session->references);

        /* TODO: may block */

        ngx_ssl_free_session(old_ssl_session);
    }
}


static ngx_int_t
ngx_http_upstream_empty_set_session(ngx_peer_connection_t *pc, void *data)
{
    return NGX_OK;
}


static void
ngx_http_upstream_empty_save_session(ngx_peer_connection_t *pc, void *data)
{
    return;
}

#endif
