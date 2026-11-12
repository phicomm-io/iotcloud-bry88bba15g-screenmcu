/* Application UART owner. Recovery variables live in internal SRAM; media
 * arenas are touched only after normal_ready, after SDRAM initialization. */
static media_parser media_uart IRAM;
static struct {
  uint32_t rate, pending, switch_at, deadline, last_ping, client;
  uint32_t command, signature, socket_sequence;
  uint8_t phase, result, operation;
  bool ready;
} media_link_state IRAM;
static bool media_board_send(unsigned channel, uint32_t sequence, const void *p,
                             size_t n) {
  /* UART TX has one foreground owner; SRAM also works before SDRAM startup. */
  static uint8_t wire[2 * (MEDIA_PACKET_MAX + 18) + 2] IRAM;
  size_t size =
      media_encode(wire, sizeof(wire), channel, guard.session, sequence, p, n);
  return size && board_send(NULL, (const char *)wire, size);
}
static size_t media_board_status(uint8_t *p, size_t cap) {
  if (!normal_ready || cap < MEDIA_STATUS_BYTES + MEDIA_STREAM_STATUS_BYTES + MEDIA_DECODER_DETAIL_BYTES)
    return 0;
  size_t n = media_status(&media, p, cap);
  board_jpeg_status(p + 24);
  size_t stream_at = n;
  n += stream_status(&stream, p + n, cap - n);
  media_put32(p + stream_at + 40, media_source.notice_id);
  board_jpeg_registers(p + n);
  n += MEDIA_DECODER_DETAIL_BYTES;
  return n;
}
/* Only one socket operation may be in flight in either direction. */
static struct {
  uint8_t bytes[MEDIA_PACKET_MAX];
  size_t size;
  uint32_t sequence, sent;
  unsigned attempts;
} media_socket_tx;
static bool media_board_socket(void *ctx, const uint8_t *p, size_t n) {
  (void)ctx;
  if (!media_link_state.ready || maintenance.state || n > sizeof(media_socket_tx.bytes) ||
      (media_socket_tx.size && p[0] != MEDIA_NET_CLOSE))
    return false;
  memcpy(media_socket_tx.bytes, p, n);
  media_socket_tx.size = n;
  media_socket_tx.sequence = ++media_link_state.socket_sequence;
  media_socket_tx.attempts = 0;
  media_socket_tx.sent = 0;
  return true;
}
static void media_board_packet(void *ctx, unsigned channel, uint32_t session,
                               uint32_t sequence, const uint8_t *p, size_t n) {
  (void)ctx;
  if (session != guard.session || !n)
    return;
  if (channel == MEDIA_LINK) {
    if (p[0] == MEDIA_PING && n == 5) {
      uint32_t client = media_u32(p + 1);
      if (!client)
        return;
      if (client != media_link_state.client) {
        media_link_state.client = client;
        media_link_state.command = 0;
        media_link_state.socket_sequence = 0;
        if (normal_ready) {
          media_socket_tx.size = 0;
          stream.state = stream.enabled ? STREAM_PAUSED : STREAM_OFF;
          stream.request_size = 0;
          stream.ready = false;
        }
      }
      media_link_state.last_ping = board_ms;
      media_link_state.ready = !media_link_state.phase;
      uint8_t reply[MEDIA_PACKET_MAX] = {MEDIA_PONG};
      media_put32(reply + 1, media_link_state.rate);
      reply[5] = media_link_state.phase;
      size_t bytes = 6;
      if (normal_ready)
        bytes += media_board_status(reply + 6, sizeof(reply) - 6);
      media_board_send(MEDIA_LINK, sequence, reply, bytes);
      return;
    }
    if (p[0] == MEDIA_BAUD && n == 5 && !normal_ready && !maintenance.state &&
        !media_link_state.phase &&
        media_rate_valid(media_u32(p + 1))) {
      media_link_state.pending = media_u32(p + 1);
      media_link_state.phase = 1;
      media_link_state.ready = false;
      media_link_state.switch_at = board_ms + 100;
      media_link_state.deadline = board_ms + 2500;
      uint8_t reply[5] = {MEDIA_BAUD_READY};
      media_put32(reply + 1, media_link_state.pending);
      media_board_send(MEDIA_LINK, sequence, reply, sizeof(reply));
      return;
    }
    if (p[0] == MEDIA_BAUD_PROBE && n == 65 && media_link_state.phase == 2) {
      for (unsigned i = 1; i < 65; i++)
        if (p[i] != (uint8_t)(i * 73u + 19u))
          return;
      media_link_state.phase = 3;
      media_board_send(MEDIA_LINK, sequence, p, n);
      return;
    }
    if (p[0] == MEDIA_BAUD_CONFIRM && n == 5 && media_link_state.phase == 3 &&
        media_u32(p + 1) == media_link_state.rate) {
      media_link_state.phase = 0;
      media_link_state.ready = true;
      media_link_state.last_ping = board_ms;
      media_board_send(MEDIA_LINK, sequence, p, n);
      return;
    }
    return;
  }
  if (!normal_ready || !media_link_state.ready || maintenance.state)
    return;
  if (channel == MEDIA_COMMAND) {
    unsigned result = MEDIA_RANGE;
    uint32_t signature = media_crc32(p, n);
    if (sequence == media_link_state.command &&
        signature == media_link_state.signature)
      result = media_link_state.result;
    else if (sequence > media_link_state.command) {
      if (p[0] == MEDIA_STREAM_CONFIG)
        result = media_source_config(&media_source, &stream, p, n);
      else if ((p[0] == MEDIA_STREAM_START || p[0] == MEDIA_STREAM_STOP) &&
               n == 1) {
        uint8_t config[MEDIA_STREAM_CONFIG_BYTES];
        memcpy(config, media_source.page, sizeof(config));
        config[6] = (config[6] & ~1u) | (p[0] == MEDIA_STREAM_START);
        result = config[7] ? media_source_config(&media_source, &stream, config, sizeof(config)) : MEDIA_RANGE;
      } else
        result = media_command(&media, p, n);
      media_link_state.command = sequence;
      media_link_state.signature = signature;
      media_link_state.result = result;
      media_link_state.operation = p[0];
    }
    uint8_t reply[MEDIA_PACKET_MAX] = {p[0], result};
    size_t bytes = 2 + media_board_status(reply + 2, sizeof(reply) - 2);
    media_board_send(MEDIA_REPLY, sequence, reply, bytes);
  } else if (channel == MEDIA_SOCKET_ACK) {
    if (n == 5 && sequence == media_socket_tx.sequence && media_socket_tx.size >= 5 &&
        !memcmp(p, media_socket_tx.bytes, 5))
      media_socket_tx.size = 0;
  } else if (channel == MEDIA_SOCKET || channel == MEDIA_SOCKET_EVENT) {
    stream_socket(&stream, sequence, p, n);
    if (channel == MEDIA_SOCKET)
      media_board_send(MEDIA_SOCKET_ACK, sequence, p, n < 5 ? n : 5);
  }
}
static void media_board_maintenance_released(void) {
  // A synchronous NOR rehash can finish the last two OTA phases in one pump.
  // Give the media owner a fresh heartbeat grace period even in that case.
  media_link_state.last_ping = board_ms;
}
static void media_board_link_poll(void) {
  uint32_t now = board_ms;
  // Raw OTA suspends media heartbeats and can run longer than their timeout.
  // Keep its negotiated UART rate until the maintenance owner releases it.
  if (maintenance.state) {
    media_link_state.last_ping = now;
    return;
  }
  if (media_link_state.phase == 1 &&
      (int32_t)(now - media_link_state.switch_at) >= 0) {
    if (board_uart_baud(media_link_state.pending)) {
      media_link_state.rate = media_link_state.pending;
      media_link_state.phase = 2;
    } else
      media_link_state.deadline = now;
    maintenance.used = 0;
    media_uart.active = false;
  }
  if ((media_link_state.phase &&
       (int32_t)(now - media_link_state.deadline) >= 0) ||
      (media_link_state.rate != 115200 &&
       (uint32_t)(now - media_link_state.last_ping) > 10000)) {
    board_uart_baud(115200);
    media_link_state.rate = 115200;
    media_link_state.phase = 0;
    media_link_state.ready = false;
    media_uart.active = false;
    maintenance.used = 0;
    guard_announce_at = now;
  }
  if (media_link_state.ready &&
      (uint32_t)(now - media_link_state.last_ping) > 10000)
    media_link_state.ready = false;
}

static void media_board_socket_poll(void) {
  if (!normal_ready) return;
  if (!media_link_state.ready || maintenance.state) { media_socket_tx.size = 0; return; }
  if (media_socket_tx.size && (!media_socket_tx.sent ||
      (uint32_t)(board_ms - media_socket_tx.sent) > 1500)) {
    if (media_socket_tx.attempts >= 3) {
      uint8_t error[5] = {MEDIA_NET_ERROR};
      memcpy(error + 1, media_socket_tx.bytes + 1, 4);
      media_socket_tx.size = 0;
      stream_socket(&stream, 0, error, sizeof(error));
      return;
    }
    if (media_board_send(MEDIA_SOCKET_EVENT, media_socket_tx.sequence,
                         media_socket_tx.bytes, media_socket_tx.size)) {
      media_socket_tx.sent = board_ms;
      media_socket_tx.attempts++;
    }
  }
}
