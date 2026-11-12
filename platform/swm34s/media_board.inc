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
  uint8_t wire[2 * (MEDIA_PACKET_MAX + 18) + 2];
  size_t size =
      media_encode(wire, sizeof(wire), channel, guard.session, sequence, p, n);
  return size && board_send(NULL, (const char *)wire, size);
}
static size_t media_board_status(uint8_t *p, size_t cap) {
  if (!normal_ready || cap < 188)
    return 0;
  size_t n = media_status(&media, p, cap);
  n += stream_status(&stream, p + n, cap - n);
  return n;
}
static bool media_board_socket(void *ctx, const uint8_t *p, size_t n) {
  (void)ctx;
  return media_link_state.ready && !maintenance.state &&
         media_board_send(MEDIA_SOCKET_EVENT,
                          ++media_link_state.socket_sequence, p, n);
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
        !guard.forced && !media_link_state.phase &&
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
        result = stream_config(&stream, p, n);
      else if ((p[0] == MEDIA_STREAM_START || p[0] == MEDIA_STREAM_STOP) &&
               n == 1) {
        stream.enabled = p[0] == MEDIA_STREAM_START;
        result = stream.url[0] ? MEDIA_OK : MEDIA_RANGE;
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
  } else if (channel == MEDIA_SOCKET || channel == MEDIA_SOCKET_EVENT) {
    stream_socket(&stream, sequence, p, n);
    if (channel == MEDIA_SOCKET)
      media_board_send(MEDIA_SOCKET_ACK, sequence, p, n < 5 ? n : 5);
  }
}
static void media_board_link_poll(void) {
  uint32_t now = board_ms;
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
