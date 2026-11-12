#include "media.h"
#include "media_wire.h"
#include "stream.h"
#include "media_source.h"
#include <assert.h>
#include <string.h>
#define IRAM
static bool normal_ready;
static uint32_t board_ms, guard_announce_at;
static struct { uint32_t session; } guard;
static struct { unsigned state, used; } maintenance;
static unsigned baud_changes;
static bool board_send(void *ctx, const char *p, size_t n) {
  (void)ctx; (void)p; return n != 0;
}
static bool board_uart_baud(uint32_t rate) {
  assert(media_rate_valid(rate)); ++baud_changes; return true;
}
static void board_jpeg_status(uint8_t *p) { memset(p, 0, 4); }
static void board_jpeg_registers(uint8_t *p) { memset(p, 0, 64); }
#include "../platform/swm34s/media_board.inc"

int main(void) {
  guard.session = 123;
  media_uart.packet = media_board_packet;
  media_link_state.rate = 921600;
  media_link_state.ready = true;
  media_link_state.last_ping = 100;
  normal_ready = true;
  // Every raw OTA phase can exceed the media heartbeat timeout. Neither a
  // baud reset nor a socket write may interrupt the maintenance receiver.
  for (unsigned state = 1; state <= 5; state++) {
    maintenance.state = state; board_ms += 40000;
    media_board_link_poll();
    assert(media_link_state.rate == 921600 && !baud_changes);
    assert(media_link_state.last_ping == board_ms);
    uint8_t open[5] = {MEDIA_NET_OPEN};
    assert(!media_board_socket(NULL, open, sizeof(open)));
    media_socket_tx.size = 5; media_board_socket_poll();
    assert(!media_socket_tx.size);
  }
  maintenance.state = 0;
  board_ms += 40000;
  media_board_maintenance_released();
  board_ms += 9999; media_board_link_poll();
  assert(!baud_changes && media_link_state.ready);
  board_ms += 2; media_board_link_poll();
  assert(baud_changes == 1 && media_link_state.rate == 115200);
  assert(!media_link_state.ready && guard_announce_at == board_ms);
  uint8_t ping[5] = {MEDIA_PING, 1};
  media_board_packet(NULL, MEDIA_LINK, guard.session, 1, ping, sizeof(ping));
  assert(media_link_state.ready);
  media_init(&media);stream_init(&stream);media_source_init(&media_source);
  uint8_t push[535]={MEDIA_PUSH_URL};media_put32(push+1,90);media_put32(push+9,1000);push[13]=1;push[14]=30;
  memcpy(push+16,media_source.page,519);push[22]=3;strcpy((char *)push+23,"http://camera.test/event");
  media_board_packet(NULL,MEDIA_COMMAND,guard.session,10,push,sizeof(push));
  assert(media_count(&media)==1&&media.last_push_id==90);media.queue[0].remaining=200;
  media_board_packet(NULL,MEDIA_COMMAND,guard.session,10,push,sizeof(push));assert(media.queue[0].remaining==200);
  media_board_packet(NULL,MEDIA_COMMAND,guard.session,11,push,sizeof(push));assert(media.queue[0].remaining==1000);
  uint8_t update[18]={MEDIA_PUSH_UPDATE};media_put32(update+1,90);
  update[16]=MEDIA_UPDATE_DURATION;media_put32(update+9,4000);
  media_board_packet(NULL,MEDIA_COMMAND,guard.session,12,update,sizeof(update));
  assert(media.queue[0].remaining==4000);media.queue[0].remaining=3000;
  media_board_packet(NULL,MEDIA_COMMAND,guard.session,12,update,sizeof(update));
  assert(media.queue[0].remaining==3000); /* Lost reply retries do not renew twice. */
  media_board_packet(NULL,MEDIA_COMMAND,guard.session,13,update,sizeof(update));
  assert(media.queue[0].remaining==4000);


}
