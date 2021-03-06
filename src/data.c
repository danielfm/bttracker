/*
 * Copyright (c) 2013, BtTracker Authors
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

void
bt_bytearray_to_hexarray(int8_t *bin, size_t binsz, char **result)
{
  const char *hex_str = "0123456789abcdef";

  *result = (char *) malloc(binsz * 2 + 1);
  (*result)[binsz * 2] = 0;

  if (!binsz) {
    return;
  }

  for (int i = 0; i < binsz; i++) {
    (*result)[i * 2 + 0] = hex_str[(bin[i] >> 4) & 0xF];
    (*result)[i * 2 + 1] = hex_str[bin[i] & 0x0F];
  }
}

redisContext *
bt_redis_connect(const char *unix_sock, const char *host, int port,
                 long timeout, int db)
{
  redisContext *conn;
  redisReply *reply;
  struct timeval timeout_val = {0, timeout};

  if (NULL != unix_sock) {
    syslog(LOG_DEBUG, "Connecting to Redis instance at %s", unix_sock);
    conn = redisConnectUnixWithTimeout(unix_sock, timeout_val);
  } else {
    syslog(LOG_DEBUG, "Connecting to Redis instance at %s:%d[%d]", host, port, db);
    conn = redisConnectWithTimeout(host, port, timeout_val);
  }

  if (NULL == conn) {
    syslog(LOG_ERR, "Connection error: can't allocate conn context");
    return NULL;
  }

  if (conn->err) {
    syslog(LOG_ERR, "Connection error: %s", conn->errstr);
    redisFree(conn);
    return NULL;
  }

  syslog(LOG_DEBUG, "Connection with Redis instance established");

  /* Switching to the configured database. */
  reply = redisCommand(conn, "SELECT %d", db);

  if (reply != NULL) {
    freeReplyObject(reply);
    syslog(LOG_DEBUG, "Redis database switched to %d", db);
  }

  return conn;
}

bool
bt_redis_ping(redisContext *redis)
{
  bool ok = false;
  redisReply *reply;

  reply = redisCommand(redis, "PING");

  if (reply != NULL) {
    ok = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
  }

  return ok;
}

void
bt_insert_connection(redisContext *redis, const bt_config_t *config,
                     int64_t connection_id)
{
  redisReply *reply;

  reply = redisCommand(redis, "SETEX %s:conn:%b %d 1",
                       config->redis_key_prefix, &connection_id,
                       sizeof(int64_t), BT_ACTIVE_CONNECTION_TTL);

  if (NULL == reply) {
    syslog(LOG_ERR, "Got a NULL reply from Redis");
    return;
  }

  if (REDIS_REPLY_ERROR == reply->type) {
    syslog(LOG_ERR, "Cannot store connection");
  } else {
    syslog(LOG_DEBUG, "Connection stored successfully");
  }

  freeReplyObject(reply);
}

bool
bt_connection_valid(redisContext *redis, const bt_config_t *config,
                    int64_t connection_id)
{
  bool valid = false;
  redisReply *reply;

  reply = redisCommand(redis, "GET %s:conn:%b",
                       config->redis_key_prefix, &connection_id,
                       sizeof(int64_t));

  if (NULL == reply) {
    syslog(LOG_ERR, "Got a NULL reply from Redis");
    return false;
  }

  valid = reply->type != REDIS_REPLY_ERROR;

  freeReplyObject(reply);
  return valid;
}

bt_peer_t *
bt_new_peer(bt_announce_req_t *request, uint32_t sockaddr)
{
  bt_peer_t *peer = (bt_peer_t *) malloc(sizeof(bt_peer_t));

  peer->key        = request->key;
  peer->downloaded = request->downloaded;
  peer->uploaded   = request->uploaded;
  peer->left       = request->left;
  peer->port       = request->port;
  peer->ipv4_addr  = request->ipv4_addr == 0 ? sockaddr : request->ipv4_addr;

  return peer;
}

bt_peer_addr_t *
bt_new_peer_addr(uint32_t ipv4_addr, uint16_t port)
{
  bt_peer_addr_t *peer_addr = (bt_peer_addr_t *) malloc(sizeof(bt_peer_addr_t));

  peer_addr->ipv4_addr = ipv4_addr;
  peer_addr->port = port;

  return peer_addr;
}

void
bt_insert_peer(redisContext *redis, const bt_config_t *config,
               const char *info_hash_str, const int8_t *peer_id,
               const bt_peer_t *peer_data, bool is_seeder)
{
  redisReply *reply;
  char *peer_prefix = is_seeder ? "sd" : "lc";

  reply = redisCommand(redis, "SETEX %s:pr:%s:%s:%b %d %b",
                       config->redis_key_prefix, info_hash_str,
                       peer_prefix, peer_id, (size_t) 20,
                       config->announce_peer_ttl, peer_data, sizeof(bt_peer_t));

  if (NULL == reply) {
    syslog(LOG_ERR, "Got a NULL reply from Redis");
    return;
  }

  if (REDIS_REPLY_ERROR == reply->type) {
    syslog(LOG_ERR, "Cannot store peer data");
  } else {
    syslog(LOG_DEBUG, "Peer data stored successfully");
  }

  freeReplyObject(reply);
}

void
bt_remove_peer(redisContext *redis, const bt_config_t *config,
               const char *info_hash_str, const int8_t *peer_id,
               bool is_seeder)
{
  redisReply *reply;
  char *peer_prefix = is_seeder ? "sd" : "lc";

  reply = redisCommand(redis, "DEL %s:pr:%s:%s:%b",
                       config->redis_key_prefix, info_hash_str,
                       peer_prefix, peer_id, (size_t) 20);

  if (NULL == reply) {
    syslog(LOG_ERR, "Got a NULL reply from Redis");
    return;
  }

  if (REDIS_REPLY_INTEGER == reply->type && 1 == reply->integer) {
    syslog(LOG_DEBUG, "Peer data removed successfully");
  } else {
    syslog(LOG_ERR, "Cannot remove peer data");
  }

  freeReplyObject(reply);
}

void
bt_promote_peer(redisContext *redis, const bt_config_t *config,
                const char *info_hash_str, const int8_t *peer_id)
{
  redisReply *reply;

  reply = redisCommand(redis, "RENAME %s:pr:%s:lc:%b %s:pr:%s:sd:%b",
                       config->redis_key_prefix, info_hash_str, peer_id,
                       (size_t) 20, config->redis_key_prefix, info_hash_str,
                       peer_id, (size_t) 20);

  if (NULL == reply) {
    syslog(LOG_ERR, "Got a NULL reply from Redis");
    return;
  }

  if (REDIS_REPLY_ERROR == reply->type) {
    syslog(LOG_ERR, "Cannot promote peer");
  } else {
    syslog(LOG_DEBUG, "Peer promoted from leecher to seeder");

    /* Increments the number of times this torrent was downloaded. */
    bt_increment_downloads(redis, config, info_hash_str);
  }

  freeReplyObject(reply);
}

void
bt_increment_downloads(redisContext *redis, const bt_config_t *config,
                       const char *info_hash_str)
{
  redisReply *reply;

  reply = redisCommand(redis, "HINCRBY %s:ih:%s downs 1",
                       config->redis_key_prefix, info_hash_str);

  if (NULL == reply) {
    syslog(LOG_ERR, "Got a NULL reply from Redis");
    return;
  }

  if (REDIS_REPLY_INTEGER == reply->type) {
    syslog(LOG_DEBUG, "Updated download counter for torrent");
  }

  freeReplyObject(reply);
}

bool
bt_info_hash_blacklisted(redisContext *redis, const bt_config_t *config,
                         const char *info_hash_str)
{
  redisReply *reply;
  bool blacklisted = true;

  switch (config->info_hash_restriction) {
  case BT_RESTRICTION_WHITELIST:
    reply = redisCommand(redis, "SISMEMBER %s:ih:wl %s",
                         config->redis_key_prefix, info_hash_str);

    if (NULL == reply) {
      syslog(LOG_ERR, "Got a NULL reply from Redis");
    } else {
      blacklisted = (REDIS_REPLY_INTEGER == reply->type && 0 == reply->integer);
      freeReplyObject(reply);
    }
    break;

  case BT_RESTRICTION_BLACKLIST:
    reply = redisCommand(redis, "SISMEMBER %s:ih:bl %s",
                         config->redis_key_prefix, info_hash_str);

    if (NULL == reply) {
      syslog(LOG_ERR, "Got a NULL reply from Redis");
    } else {
      blacklisted = (REDIS_REPLY_INTEGER == reply->type && reply->integer > 0);
      freeReplyObject(reply);
    }
    break;

  case BT_RESTRICTION_NONE:
  default:
    blacklisted = false;
  }

  return blacklisted;
}

/* Fills `stats` with the latests stats for a torrent. */
void
bt_get_torrent_stats(redisContext *redis, const bt_config_t *config,
                     const char *info_hash_str, bt_torrent_stats_t *stats)
{
  redisReply *reply;

  /* Counts the number of seeders. */
  redisAppendCommand(redis, "KEYS %s:pr:%s:sd:*",
                     config->redis_key_prefix, info_hash_str);

  /* Counts the number of leechers. */
  redisAppendCommand(redis, "KEYS %s:pr:%s:lc:*",
                     config->redis_key_prefix, info_hash_str);

  /* Returns the number of times this torrent has been downloaded. */
  redisAppendCommand(redis, "HGET %s:ih:%s downs",
                     config->redis_key_prefix, info_hash_str);

  if (redisGetReply(redis, (void **) &reply) == REDIS_OK) {
    stats->seeders = reply->elements;
  }
  if (reply != NULL) {
    freeReplyObject(reply);
  }

  if (redisGetReply(redis, (void **) &reply) == REDIS_OK) {
    stats->leechers = reply->elements;
  }
  if (reply != NULL) {
    freeReplyObject(reply);
  }

  if (redisGetReply(redis, (void **) &reply) == REDIS_OK) {
    stats->downloads = !reply->str ? 0 : strtoimax(reply->str, NULL, 10);
  }
  if (reply != NULL) {
    freeReplyObject(reply);
  }
}

bt_list *
bt_peer_list(redisContext *redis, const bt_config_t *config,
             const char *info_hash_str, int32_t num_want,
             int *peer_count, bool seeder)
{
  redisReply *reply;

  int count = 0;
  bt_list *list = NULL;

  /* We give seeders for leechers, and leechers for seeders. */
  char *peer_prefix = seeder ? "sd" : "lc";

  reply = redisCommand(redis, "KEYS %s:pr:%s:%s:*",
                       config->redis_key_prefix, info_hash_str, peer_prefix);

  if (NULL == reply) {
    syslog(LOG_ERR, "Got a NULL reply from Redis");
    return NULL;
  }

  if (REDIS_REPLY_ARRAY == reply->type) {
    size_t total_keys = reply->elements;
    int i, upper_index = total_keys > num_want ? num_want : total_keys;

    /* Shuffles the keys a little bit to ensure all peers have a chance. */
    for (i = 0; i < upper_index; i++) {
      redisReply *aux = (redisReply *) reply->element[i];
      int j = randr(i, total_keys-1);

      reply->element[i] = reply->element[j];
      reply->element[j] = aux;
    }

    /* Pipelines the GET commands for all keys. */
    for (i = 0; i < upper_index; i++) {
      redisAppendCommand(redis, "GET %b", reply->element[i]->str,
                         reply->element[i]->len);
    }

    freeReplyObject(reply);

    /* Now we get the peer data stored under each key. */
    for (i = 0; i < upper_index; i++) {

      /* Extracts the peer address and appends it to the list. */
      if (redisGetReply(redis, (void **) &reply) == REDIS_OK) {
        bt_peer_t *peer_data = (bt_peer_t *) reply->str;

        bt_peer_addr_t *addr = bt_new_peer_addr(peer_data->ipv4_addr,
                                                peer_data->port);
        freeReplyObject(reply);

        list = bt_list_prepend(list, addr);
        count++;
      } else {
        syslog(LOG_INFO, "Unable to get peer data");
      }
    }
  }

  *peer_count = count;
  return list;
}
