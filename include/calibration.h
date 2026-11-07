#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { CAL_CO2=1, CAL_TVOC=2 };
enum { CAL_SNAPSHOT=1, CAL_COMMAND, CAL_REPORT };
typedef bool (*calibration_emit)(void *,unsigned channel,unsigned event,const uint8_t *,size_t);
/* These status slots deliberately remain separate from screen metric status:
 * stock CO2 executor writes RAM aa8+7 (also used as PM2.5 snapshot status). */
typedef struct {
 uint8_t stage,issued,index,status,saved_status;
 uint16_t wait,samples[10];
} calibration_job;
typedef struct {
 uint8_t pending;
 uint16_t network_awake;
 bool co2_command_pending;
 calibration_job jobs[2];
} sensor_calibration;
void calibration_init(sensor_calibration *);
/* Community-local/negotiated-extension entry; never called on an ordinary ACK.
 * Returns false when another job is running or channels are invalid.
 * Caller must provide current sensor status and age to poll. This local API
 * shares the same engine as the existing stock service; no new wire command. */
bool calibration_request(sensor_calibration *,unsigned channels);
/* Call once per factory wifi-loop cadence, NOT per millisecond. age is the
 * stock +0x60 counter; caller advances it. 120 polls/sample, ten samples. The
 * result report code 3 means command queued, NOT a sensor acknowledgement.
 * emit must return true only when the command is accepted by its UART owner.
 * Rejected sends produce a failure report; no callback leaves state unchanged. */
void calibration_poll(sensor_calibration *,uint32_t age,uint16_t co2,uint16_t tvoc,
 calibration_emit,void *context);
/* Stock service/calibration after JSON/client_token validation. Both keys are
 * mandatory; value 1 adds a pending channel, other values preserve its old bit.
 * ACK means accepted, not calibrated. False = no response (busy/missing keys).
 * Duplicate active requests are safely rejected: stock skips token lookup and
 * dereferences NULL there. Stage and sensor result are advanced only by poll. */
bool calibration_factory_service(sensor_calibration *,bool has_co2,int co2,
 bool has_tvoc,int tvoc);
/* Stock hcho_install checks client_token and ACKs only; no sensor side effect. */
bool calibration_hcho_install(bool has_client_token);
