/* Created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 * Uses the RMT peripheral on the ESP32 for very accurate timing of
 * signals sent to the WS281x LEDs.
 *
 * This code is placed in the public domain (or CC0 licensed, at your option).
 *
 * Adapted to p44utils context by Lukas Zeller <luz@plan44.ch>
 *
 */

#include "esp32_ws281x.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <soc/rmt_struct.h>
#include <soc/dport_reg.h>
#include <driver/gpio.h>
#include <soc/gpio_sig_map.h>
#include <esp_intr.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <driver/rmt.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "ws281x";

#define ETS_RMT_CTRL_INUM	18
#define ESP_RMT_CTRL_DISABLE	ESP_RMT_CTRL_DIABLE /* Typo in esp_intr.h */

#define DIVIDER		4 /* Above 4, timings start to deviate*/
#define DURATION	12.5 /* minimum time of a single RMT duration
				in nanoseconds based on clock */

#define PULSE_T0H	   375
#define PULSE_T0L	   875
#define PULSE_T1H	   750
#define PULSE_T1L	   500
#define PULSE_TRS	300000

#define PULSE_TO_RMTDELAY(t) ((uint16_t)((double)t/(DURATION*DIVIDER)))

#define MAX_PULSES_RELOAD_TIME_US (MAX_PULSES*(PULSE_T0H+PULSE_T1H)/1000)

#define TIMING_DEBUG 0 // if set, console shows some statistics about reload delay (IRQ response), retries and errors
#define GPIO_LOGICANALYZER_OUTPUT 0 // if set, GPIO 21 and 22 output additional signals to catch reload timing and slow IRQs with a logic analyzer

#define MEM_BLOCKS_PER_CHANNEL 1 // how many mem blocks to use. Can be set to max 8 when no other RMT unit is in use

#define MAX_PULSES	(32*MEM_BLOCKS_PER_CHANNEL) // number of pulses per half-block

#define RMTCHANNEL	0

typedef union {
  struct {
    uint32_t duration0:15;
    uint32_t level0:1;
    uint32_t duration1:15;
    uint32_t level1:1;
  };
  uint32_t val;
} rmtPulsePair;

static uint8_t *ws281x_buffer = NULL;
static unsigned int ws281x_pos, ws281x_len, ws281x_half;
static xSemaphoreHandle ws281x_sem = NULL;
static intr_handle_t rmt_intr_handle = NULL;
static rmtPulsePair ws281x_bits[2];


#define MAX_RESEND_RETRIES 3 // number of retries in case of timing fail, 0=none
static int retries = 0;
#if TIMING_DEBUG
static int64_t timeOfLastLoad = 0;
static int64_t minReloadTime = 1000000; // should be below one second
static int64_t maxReloadTime = 0;
static int totalRetries = 0;
static int totalErrors = 0;
#endif




void ws281x_initRMTChannel(int rmtChannel)
{
  RMT.apb_conf.fifo_mask = 1;  //enable memory access, instead of FIFO mode.
  RMT.apb_conf.mem_tx_wrap_en = 1; //wrap around when hitting end of buffer
  RMT.conf_ch[rmtChannel].conf0.div_cnt = DIVIDER;
  RMT.conf_ch[rmtChannel].conf0.mem_size = MEM_BLOCKS_PER_CHANNEL;
  RMT.conf_ch[rmtChannel].conf0.carrier_en = 0;
  RMT.conf_ch[rmtChannel].conf0.carrier_out_lv = 1;
  RMT.conf_ch[rmtChannel].conf0.mem_pd = 0;

  RMT.conf_ch[rmtChannel].conf1.rx_en = 0;
  RMT.conf_ch[rmtChannel].conf1.mem_owner = 0;
  RMT.conf_ch[rmtChannel].conf1.tx_conti_mode = 0;    //loop back mode.
  RMT.conf_ch[rmtChannel].conf1.ref_always_on = 1;    // use apb clock: 80M
  RMT.conf_ch[rmtChannel].conf1.idle_out_en = 1;
  RMT.conf_ch[rmtChannel].conf1.idle_out_lv = 0;

  return;
}


// copy half the RMT transmit buffer (MAX_PULSES number of pulses)
// each of the 8 RMT channel has a buffer for 512/8 = 64 pulses, so half normally is 32 pulses @ MEM_BLOCKS_PER_CHANNEL==1
// in addition, a safety stop (for when IRQ is delayed too long) is placed in the first pulse of the *other* half buffer
void ws281x_copy()
{
  unsigned int i, j, offset, len, ledbyte;

  #if TIMING_DEBUG
  int64_t now =  esp_timer_get_time();
  #endif

  offset = ws281x_half * MAX_PULSES; // alternating offset to beginning or middle of RMT tx buffer
  ws281x_half = !ws281x_half; // alternate

  len = ws281x_len - ws281x_pos; // remaining bytes to send
  if (len > (MAX_PULSES / 8)) {
    len = (MAX_PULSES / 8); // limit to an even number of bytes
  }

  // convert len bytes to pulses (if any)
  for (i = 0; i < len; i++) {
    ledbyte = ws281x_buffer[i + ws281x_pos]; // get the byte
    for (j = 0; j < 8; j++, ledbyte <<= 1) {
      // set the high and low pulse part of this bit (from ws281x_bits[] template)
      RMTMEM.chan[RMTCHANNEL].data32[j + i * 8 + offset].val =
        ws281x_bits[(ledbyte >> 7) & 0x01].val;
    }
    // modify the duration of the last low pulse to become reset if this was the last byte
    if (i + ws281x_pos == ws281x_len - 1) {
      RMTMEM.chan[RMTCHANNEL].data32[7 + i * 8 + offset].duration1 = PULSE_TO_RMTDELAY(PULSE_TRS);
    }
  }

  // fill remaining pulses in this half block with TX end markers
  for (i *= 8; i < MAX_PULSES; i++) {
    RMTMEM.chan[RMTCHANNEL].data32[i + offset].val = 0; // TX end marker
  }
  ws281x_pos += len; // update pointer
  // Now assuming (quite safely, as IRQ response time<2uS is impossible) that the first pulse of
  // the other (now running) block half is already out by now, overwrite it with a
  // reset-length 0 and a stopper.
  // In case the next IRQ is late and has NOT been able to re-fill that block, output will
  // stop without sending wrong byte data and causing visual glitches.
  // However if IRQ is in time, it will overwrite that stopper with more valid data.
  RMTMEM.chan[RMTCHANNEL].data32[ws281x_half*MAX_PULSES].val = PULSE_TO_RMTDELAY(PULSE_TRS); // <<16; // first a 0 with reset length, then stop
  #if TIMING_DEBUG
  if (timeOfLastLoad>0 && ws281x_pos<ws281x_len) {
    // still sending, update timing stats
    int64_t reloadTime = now-timeOfLastLoad;
    if (reloadTime>maxReloadTime) maxReloadTime = reloadTime;
    if (reloadTime<minReloadTime) minReloadTime = reloadTime;
  }
  timeOfLastLoad = now;
  #endif
  return;
}


void start_transfer()
{
  #if GPIO_LOGICANALYZER_OUTPUT
  gpio_set_level(22, 1);
  gpio_set_level(21, 1);
  #endif
  #if TIMING_DEBUG
  timeOfLastLoad = 0;
  #endif
  // init buffer pointers
  ws281x_pos = 0;
  ws281x_half = 0;
  // copy at least one half of data
  ws281x_copy(); // include a stopper (in case ws281x_len is exactly one half)
  // start RMT now
  // - note we must disable IRQs on this core completely to avoid starting RMT and then copying next data
  //   is not *delayed* by a long duration IRQ routine. Note that this blocking is *not* because of
  //   access to shared data (for which single core IRQ block would not help)!
  portDISABLE_INTERRUPTS();
  RMT.conf_ch[RMTCHANNEL].conf1.mem_rd_rst = 1;
  RMT.conf_ch[RMTCHANNEL].conf1.tx_start = 1;
  // - safely assuming RMT engine will have sent the first pulse long before we are done filling the second half
  //   fill the second half ALSO including a stopper overwriting the first pulse of the first half.
  //   This way, if the first THR-IRQ is too late, data will stop after two halves, avoiding send
  //   of old data in the first half a second time.
  //   If THR-IRQ is in time, it will overwrite the stopper with new data before RMT runs into it
  ws281x_copy(); // include a stopper
  portENABLE_INTERRUPTS();
}


void ws281x_handleInterrupt(void *arg)
{
  portBASE_TYPE taskAwoken = 0;

  // must check stop event first, in case we missed the tx threshold IRQ
  if (RMT.int_st.ch0_tx_end) {
    // end of transmission, transmitter entered idle state
    if (ws281x_pos<ws281x_len) {
      #if GPIO_LOGICANALYZER_OUTPUT
      gpio_set_level(22, 0);
      #endif
      // stop has occurred (because of IRQ delay) before all data was out
      if (retries<MAX_RESEND_RETRIES) {
        // restart transmission
        retries++;
        #if TIMING_DEBUG
        totalRetries++;
        #endif
        RMT.int_clr.ch0_tx_thr_event = 1; // first clear the THR IRQ, in case it is pending already
        RMT.int_clr.ch0_tx_end = 1; // ack end IRQ
        start_transfer();
        return;
      }
      else {
        #if TIMING_DEBUG
        totalErrors++;
        #endif
      }
    }
    #if GPIO_LOGICANALYZER_OUTPUT
    gpio_set_level(21, 0);
    #endif
    // - get rid of old memory buffer
    free(ws281x_buffer);
    // - unlock ws281x_setColors() again
    xSemaphoreGiveFromISR(ws281x_sem, &taskAwoken);
    RMT.int_clr.ch0_tx_thr_event = 1; // first clear the THR IRQ, in case it is pending already
    RMT.int_clr.ch0_tx_end = 1; // ack IRQ
  }
  else if (RMT.int_st.ch0_tx_thr_event) {
    // sent until middle of buffer (tx threshold)
    RMT.int_clr.ch0_tx_thr_event = 1; // ack IRQ
    ws281x_copy(); // copy new data into now-free part of buffer
  }
  return;
}


void ws281x_init(int gpioNum)
{
  #if GPIO_LOGICANALYZER_OUTPUT
  gpio_pad_select_gpio(22);
  gpio_set_direction(22, GPIO_MODE_DEF_OUTPUT);
  gpio_set_level(22, 0);
  gpio_pad_select_gpio(21);
  gpio_set_direction(21, GPIO_MODE_DEF_OUTPUT);
  gpio_set_level(21, 0);
  #endif


  // semaphore for locking buffer
  ws281x_sem = xSemaphoreCreateBinary(); // semaphore is created taken...
  xSemaphoreGive(ws281x_sem); // ...so to begin, give it, so  ws281x_setColors() can start sending

  // prepare HW
  DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_RMT_CLK_EN);
  DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_RMT_RST);

  rmt_set_pin((rmt_channel_t)RMTCHANNEL, RMT_MODE_TX, (gpio_num_t)gpioNum);

  ws281x_initRMTChannel(RMTCHANNEL);

  RMT.tx_lim_ch[RMTCHANNEL].limit = MAX_PULSES;
  RMT.int_ena.ch0_tx_thr_event = 1;
  RMT.int_ena.ch0_tx_end = 1;

  // template for 0 and 1 bit pattern
  ws281x_bits[0].level0 = 1;
  ws281x_bits[0].level1 = 0;
  ws281x_bits[0].duration0 = PULSE_TO_RMTDELAY(PULSE_T0H);
  ws281x_bits[0].duration1 = PULSE_TO_RMTDELAY(PULSE_T0L);
  ws281x_bits[1].level0 = 1;
  ws281x_bits[1].level1 = 0;
  ws281x_bits[1].duration0 = PULSE_TO_RMTDELAY(PULSE_T1H);
  ws281x_bits[1].duration1 = PULSE_TO_RMTDELAY(PULSE_T1L);

  esp_intr_alloc(ETS_RMT_INTR_SOURCE, 0, ws281x_handleInterrupt, NULL, &rmt_intr_handle);

  return;
}


void ws281x_setColors(unsigned int length, rgbVal *array)
{
  unsigned int i;

  if (xSemaphoreTake(ws281x_sem, 0)) {
    #if TIMING_DEBUG
    if (timeOfLastLoad>0) {
      ESP_LOGI(TAG,
        "reload time = %lld..%lld uS (theoretically %d), retries=%d, totalRetries=%d, totalErrors=%d",
        minReloadTime, maxReloadTime, MAX_PULSES_RELOAD_TIME_US,
        retries, totalRetries, totalErrors
      );
    }
    minReloadTime = 1000000; // should be below one second
    maxReloadTime = 0;
    #endif
    retries = 0;

    // ready for new data
    // - create output buffer
    ws281x_len = (length * 3) * sizeof(uint8_t);
    ws281x_buffer = malloc(ws281x_len);
    // - fill output buffer
    for (i = 0; i < length; i++) {
      ws281x_buffer[0 + i * 3] = array[i].g;
      ws281x_buffer[1 + i * 3] = array[i].r;
      ws281x_buffer[2 + i * 3] = array[i].b;
    }
    // - start transferring the buffer
    start_transfer();
  }
  else {
    ESP_LOGW(TAG, "ws281x_setColors called again too soon");
  }
  return;
}
