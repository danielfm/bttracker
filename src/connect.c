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

bt_response_buffer_t *bt_serialize_connection_response(bt_connection_resp_t *response_data) {
  size_t resp_length = 16;
  bt_response_buffer_t *resp_buffer = (bt_response_buffer_t *) malloc(resp_length);

  if (resp_buffer == NULL) {
    syslog(LOG_ERR, "Cannot allocate memory for response buffer");
    exit(BT_EXIT_MALLOC_ERROR);
  }

  /* Convert the response data to network byte order. */
  bt_connection_resp_to_network(response_data);

  /* Fills the response buffer object. */
  resp_buffer->length = resp_length;
  resp_buffer->data = malloc(resp_length);

  memcpy(resp_buffer->data, response_data, resp_buffer->length);

  return resp_buffer;
}

bt_response_buffer_t *bt_handle_connection(bt_req_t *request, bt_config_t *config, redisContext *redis) {
  syslog(LOG_DEBUG, "Handling connection");

  /* Ignores this request if it's not valid. */
  if (!bt_valid_request(redis, config, request)) {
    return NULL;
  }

  /* According to the spec, the connection ID must be a random 64-bit int. */
  int64_t connection_id = bt_random_int64();

  /* Inserts the new connection to the table of active connections. */
  bt_insert_connection(redis, config, connection_id);

  /* Response data to connection request. */
  bt_connection_resp_t response_data = {
    .action = request->action,
    .transaction_id = request->transaction_id,
    .connection_id = connection_id
  };

  return bt_serialize_connection_response(&response_data);
}
