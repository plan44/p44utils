/* Created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 * Uses the RMT peripheral on the ESP32 for very accurate timing of
 * signals sent to the WS281x LEDs.
 *
 * This code is placed in the public domain (or CC0 licensed, at your option).
 *
 * Adapted to p44utils context and made flicker-free 2020 by Lukas Zeller <luz@plan44.ch>
 *
 */

#include "esp32_ws281x.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <soc/rmt_struct.h>
#include <soc/dport_reg.h>
#include <driver/gpio.h>
#include <soc/gpio_sig_map.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <driver/rmt.h>
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"


static const char* TAG = "ws281x";

// Build options
#define TIMING_DEBUG 0 // if set, console shows some statistics about reload delay (IRQ response), retries and errors
#define GPIO_LOGICANALYZER_OUTPUT 0 // if set, GPIO 21 and 22 output additional signals to catch reload timing and slow IRQs with a logic analyzer

// === LED types and their parameters

typedef struct {
  const char *name; ///< name of the LED type
  int channels; ///< number of channels, 3 or 4
  uint8_t fetchIdx[4]; ///< fetch indices - at what relative index to fetch bytes from input into output stream
  int T0Active_nS; ///< active time for sending a zero bit, such that 2*T0Active_nS are a usable T1Active_nS
  int TPassive_min_nS; ///< minimum time signal must be passive after an active phase
  int T0Passive_double; ///< if set, for a 0 bit the passive time is doubled
  int TPassive_max_nS; ///< max time signal can be passive without reset occurring
  int TReset_nS; ///< time signal must be passive to reset chain
} LedTypeDescriptor_t;


// Note: time resolution is 25nS (= MT7688 PWM max resolution)
static const LedTypeDescriptor_t ledTypeDescriptors[esp_num_ledtypes] = {
  {
    // WS2811 - RGB data order
    .name = "WS2811 RGB", .channels = 3, .fetchIdx = { 0, 1, 2 },
    // timing from datasheet:
    // - T0H = 350ns..650nS
    // - T0L = 1850ns..2150nS
    // - T1H = 1050ns..1350nS
    // - T1L = 1150ns..1450nS
    // - TReset = >50µS
    .T0Active_nS = 500, .TPassive_min_nS = 1200,
    .TPassive_max_nS = 10000, .TReset_nS = 50000
  },
  {
    // WS2812, WS2812B - GRB data order
    .name = "WS2812 GRB", .channels = 3, .fetchIdx = { 1, 0, 2 },
    // timing from datasheet:
    // - T0H = 200ns..500nS
    // - T0L = 750ns..1050nS (actual max is fortunately higher, ~10uS)
    // - T1H = 750ns..1050nS
    // - T1L = 200ns..500nS (actual max is fortunately higher, ~10uS)
    // - TReset = >50µS
    .T0Active_nS = 350, .TPassive_min_nS = 900,
    .TPassive_max_nS = 10000, .TReset_nS = 50000
  },
  {
    // WS2813, WS2815 - GRB data order
    .name = "WS2813/15 GRB", .channels = 3, .fetchIdx = { 1, 0, 2 },
    // timing from datasheet:
    // - T0H = 300ns..450nS
    // - T0L = 300ns..100000nS - NOTE: 300nS is definitely not working, we're using min 650nS instead (proven ok with 200 WS2813)
    // - T1H = 750ns..1000nS
    // - T1L = 300ns..100000nS - NOTE: 300nS is definitely not working, we're using min 650nS instead (proven ok with 200 WS2813)
    // - TReset = >300µS
    // - Note: T0L/T1L of more than 40µS can apparently cause single LEDs to reset and loose bits
    .T0Active_nS = 375, .TPassive_min_nS = 500,
    .TPassive_max_nS = 40000, .TReset_nS = 300000
  },
  {
    // P9823 - RGB data order, 5mm/8mm single LEDs
    .name = "P9823 RGB", .channels = 3, .fetchIdx = { 0, 1, 2 },
    // timing from datasheet:
    // - T0H = 200ns..500nS
    // - T0L = 1210ns..1510nS
    // - T1H = 1210ns..1510nS
    // - T1L = 200ns..500nS
    // - TReset = >50µS
    // Note: the T0L and T1H seem to be wrong, using experimentally determined values
    .T0Active_nS = 425, .TPassive_min_nS = 1000,
    .TPassive_max_nS = 10000, .TReset_nS = 50000
  },
  {
    // SK2812 - GRBW data order
    .name = "SK6812 GRBW", .channels = 4, .fetchIdx = { 1, 0, 2, 3 },
    // timing from datasheet:
    // - T0H = 150ns..450nS
    // - T0L = 750ns..1050nS (actual max is fortunately higher, ~15uS)
    // - T1H = 450ns..750nS
    // - T1L = 450ns..750nS (actual max is fortunately higher, ~15uS)
    // - TReset = >50µS
    .T0Active_nS = 300, .TPassive_min_nS = 900,
    .TPassive_max_nS = 15000, .TReset_nS = 80000
  },
  {
    // WS2813, WS2815 - RGB data order
    .name = "WS2813/15 RGB", .channels = 3, .fetchIdx = { 0, 1, 2 },
    // timing from datasheet:
    // - T0H = 300ns..450nS
    // - T0L = 300ns..100000nS - NOTE: 300nS is definitely not working, we're using min 650nS instead (proven ok with 200 WS2813)
    // - T1H = 750ns..1000nS
    // - T1L = 300ns..100000nS - NOTE: 300nS is definitely not working, we're using min 650nS instead (proven ok with 200 WS2813)
    // - TReset = >300µS
    // - Note: T0L/T1L of more than 40µS can apparently cause single LEDs to reset and loose bits
    .T0Active_nS = 375, .TPassive_min_nS = 500,
    .TPassive_max_nS = 40000, .TReset_nS = 300000
  },
};


// === Hardware definitions

#define ETS_RMT_CTRL_INUM	18
#define ESP_RMT_CTRL_DISABLE	ESP_RMT_CTRL_DIABLE /* Typo in esp_intr.h */

#define DIVIDER		4 /* Above 4, timings start to deviate*/
#define DURATION	12.5 /* minimum time of a single RMT duration
				in nanoseconds based on clock */

#define PULSE_TO_RMTDELAY(t) ((uint16_t)((double)(t)/(DURATION*DIVIDER)))

#define NUM_RMT_CHANNELS 8 // number of RMT channels in total

typedef union {
  struct {
    uint32_t duration0:15;
    uint32_t level0:1;
    uint32_t duration1:15;
    uint32_t level1:1;
  };
  uint32_t val;
} rmtPulsePair;


// === Internal Variables

/// LED chain channel record
typedef struct Esp_ws281x_LedChain {
  // parameters
  Esp_ws281x_LedType ledType; // the LED type
  unsigned int rmtChannel; // the RMT channel for this chain
  unsigned int maxRetries; // how many time sending data is retried when IRQ aborts sending early, 0=no retries at all
  unsigned int pulsesPerHalfBuffer; // number of pulses per half-buffer
  // internal state
  uint8_t* leddata; // data to be sent
  unsigned int pos; // next byte to be sent relative to leddata
  unsigned int len; // total bytes to be sent
  unsigned int half; // 0 means first, 1 means second half of RMT buffer must be filled with new data next
  unsigned int retries; // number of retries already done
  xSemaphoreHandle sem; // semaphore to lock buffer while sending
  rmtPulsePair pulsebits[2]; // templates for 0 and 1 valued bit pulses
  uint32_t resetDuration; // reset pulse duration
  // statistics
  #if TIMING_DEBUG
  int64_t timeOfLastLoad;
  int64_t minReloadTime;
  int64_t maxReloadTime;
  unsigned int totalRetries;
  unsigned int totalErrors;
  #endif
} Esp_ws281x_LedChain;

static intr_handle_t rmt_intr_handle = NULL; // ESP32 interrupt handle for the RMT interrupt

static unsigned int maxChains = 8; // max number of chains we'll have maximally
static unsigned int channelspacing; // channel increment (to jump over additional RMT memory blocks used by a channel)

static Esp_ws281x_LedChain* channelLEDChains[NUM_RMT_CHANNELS]; // LEDchains active on each channel, NULL if none


// === Internal routines

// copy half the RMT transmit buffer (pulsesPerHalfBuffer number of pulses)
// each of the 8 RMT channel has a buffer for 512/8 = 64 pulses, so half normally is 32 pulses @ MEM_BLOCKS_PER_CHANNEL==1
// in addition, a safety stop (for when IRQ is delayed too long) is placed in the first pulse of the *other* half buffer
static IRAM_ATTR void copyNextHalfBuffer(Esp_ws281x_LedChain *aLedChain)
{
  unsigned int i, j, offset, len, ledbyte;

  #if TIMING_DEBUG
  int64_t now =  esp_timer_get_time();
  #endif

  offset = aLedChain->half * aLedChain->pulsesPerHalfBuffer; // alternating offset to beginning or middle of RMT tx buffer
  aLedChain->half = !aLedChain->half; // alternate
  len = aLedChain->len - aLedChain->pos; // remaining bytes to send
  if (len > (aLedChain->pulsesPerHalfBuffer / 8)) {
    len = (aLedChain->pulsesPerHalfBuffer / 8); // limit to an even number of bytes
  }
  // convert len bytes to pulses (if any)
  for (i = 0; i < len; i++) {
    ledbyte = aLedChain->leddata[i + aLedChain->pos]; // get the byte
    for (j = 0; j < 8; j++, ledbyte <<= 1) {
      // set the high and low pulse part of this bit (from aLedChain->pulsebits[] template)
      RMTMEM.chan[aLedChain->rmtChannel].data32[j + i * 8 + offset].val =
        aLedChain->pulsebits[(ledbyte >> 7) & 0x01].val;
    }
    // modify the duration of the last low pulse to become reset if this was the last byte
    if (aLedChain->pos+i == aLedChain->len-1) {
      RMTMEM.chan[aLedChain->rmtChannel].data32[7 + i * 8 + offset].duration1 = aLedChain->resetDuration;
    }
  }
  // fill remaining pulses in this half block with TX end markers
  for (i *= 8; i < aLedChain->pulsesPerHalfBuffer; i++) {
    RMTMEM.chan[aLedChain->rmtChannel].data32[i + offset].val = 0; // TX end marker
  }
  aLedChain->pos += len; // update pointer
  // Now assuming (quite safely, as IRQ response time<2uS is impossible) that the first pulse of
  // the other (now running) block half is already out by now, overwrite it with a
  // reset-length 0 and a stopper.
  // In case the next IRQ is late and has NOT been able to re-fill that block, output will
  // stop without sending wrong byte data and causing visual glitches.
  // However if IRQ is in time, it will overwrite that stopper with more valid data.
  RMTMEM.chan[aLedChain->rmtChannel].data32[aLedChain->half*aLedChain->pulsesPerHalfBuffer].val = aLedChain->resetDuration; // <<16; // first a 0 with reset length, then stop
  #if TIMING_DEBUG
  if (aLedChain->timeOfLastLoad>0 && aLedChain->pos<aLedChain->len) {
    // still sending, update timing stats
    int64_t reloadTime = now-aLedChain->timeOfLastLoad;
    if (reloadTime>aLedChain->maxReloadTime) aLedChain->maxReloadTime = reloadTime;
    if (reloadTime<aLedChain->minReloadTime) aLedChain->minReloadTime = reloadTime;
  }
  aLedChain->timeOfLastLoad = now;
  #endif
  return;
}


static IRAM_ATTR void start_transfer(Esp_ws281x_LedChain *aLedChain)
{
  #if GPIO_LOGICANALYZER_OUTPUT
  gpio_set_level(22, 1);
  gpio_set_level(21, 1);
  #endif
  #if TIMING_DEBUG
  aLedChain->timeOfLastLoad = 0;
  #endif
  // init buffer pointers
  aLedChain->pos = 0;
  aLedChain->half = 0;
  // copy at least one half of data
  copyNextHalfBuffer(aLedChain);
  // start RMT now
  // - note we must disable IRQs on this core completely to avoid starting RMT and opying next data
  //   is not *delayed* by a long duration IRQ routine. Note that this blocking is *not* because of
  //   access to shared data (for which single core IRQ block would not help)!
  portDISABLE_INTERRUPTS();
  RMT.conf_ch[aLedChain->rmtChannel].conf1.mem_rd_rst = 1;
  RMT.conf_ch[aLedChain->rmtChannel].conf1.tx_start = 1;
  // - safely assuming RMT engine will have sent the first pulse long before we are done filling the second half,
  //   now fill the second half ALSO including a stopper overwriting the first pulse of the first half.
  //   This way, if the first THR-IRQ is too late, data will stop after two halves, avoiding sending
  //   of old data in the first half a second time.
  //   If THR-IRQ is in time, it will overwrite the stopper with new data before RMT runs into it
  copyNextHalfBuffer(aLedChain);
  portENABLE_INTERRUPTS();
}


static IRAM_ATTR void ws281x_handleInterrupt(void *arg)
{
  portBASE_TYPE taskAwoken = 0;

  // same interrupt handler for all RMT interrupts, must process all RMT channels
  for (unsigned int rmtChannel=0; rmtChannel<NUM_RMT_CHANNELS; rmtChannel += channelspacing) {
    Esp_ws281x_LedChain* chainP = channelLEDChains[rmtChannel];
    if (!chainP) continue;
    // must check stop event first, in case we missed the tx threshold IRQ
    if (RMT.int_st.val & (1<<(3*rmtChannel))) { // TX_END (bits 0,3,6,9... for channels 0,1,2,3...)
      // end of transmission, transmitter entered idle state
      if (chainP->pos<chainP->len) {
        #if GPIO_LOGICANALYZER_OUTPUT
        gpio_set_level(22, 0);
        #endif
        // stop has occurred (because of IRQ delay) before all data was out
        if (chainP->retries<chainP->maxRetries) {
          // restart transmission
          chainP->retries++;
          #if TIMING_DEBUG
          chainP->totalRetries++;
          #endif
          RMT.int_clr.val =
            (1<<(24+rmtChannel)) | // clear TX_THR in case it is pending already (bits 24..31 for channels 0..7)
            (1<<(3*rmtChannel)); // as well as TX_END (bits 0,3,6,9... for channels 0,1,2,3...)
          start_transfer(chainP);
          return;
        }
        else {
          #if TIMING_DEBUG
          chainP->totalErrors++;
          #endif
        }
      }
      #if GPIO_LOGICANALYZER_OUTPUT
      gpio_set_level(21, 0);
      #endif
      // - get rid of old memory buffer
      free(chainP->leddata);
      // - unlock setColors() again
      xSemaphoreGiveFromISR(chainP->sem, &taskAwoken);
      // - ack the IRQs
      RMT.int_clr.val =
        (1<<(24+rmtChannel)) | // clear TX_THR in case it is pending already (bits 24..31 for channels 0..7)
        (1<<(3*rmtChannel)); // as well as TX_END (bits 0,3,6,9... for channels 0,1,2,3...)
    }
    else if (RMT.int_st.val & (1<<(24+rmtChannel))) {
      // sent until middle of buffer (tx threshold)
      RMT.int_clr.val = (1<<(24+rmtChannel)); // acknowledge TX_THR irq (bits 24..31 for channels 0..7)
      copyNextHalfBuffer(chainP); // copy new data into now-free part of buffer
    }
  }
  return;
}


// === External API implementation

void esp_ws281x_init(unsigned int aMaxChains)
{
  for (unsigned int i=0; i<NUM_RMT_CHANNELS; i++) {
    channelLEDChains[i] = NULL; // no chain assigned yet
  }
  maxChains = aMaxChains>8 ? 8 : (aMaxChains<1 ? 1 : aMaxChains); // limit to something between 1..8
  channelspacing = NUM_RMT_CHANNELS/maxChains; // the less channels, the more memory one channel can use
  // hardware debug
  #if GPIO_LOGICANALYZER_OUTPUT
  gpio_pad_select_gpio(22);
  gpio_set_direction(22, GPIO_MODE_DEF_OUTPUT);
  gpio_set_level(22, 0);
  gpio_pad_select_gpio(21);
  gpio_set_direction(21, GPIO_MODE_DEF_OUTPUT);
  gpio_set_level(21, 0);
  #endif
  // prepare HW
  DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_RMT_CLK_EN);
  DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_RMT_RST);
  // allocate interrupt
  esp_intr_alloc(ETS_RMT_INTR_SOURCE, 0, ws281x_handleInterrupt, NULL, &rmt_intr_handle);
  // RMT setup for all channels
  RMT.apb_conf.fifo_mask = 1;  // enable memory access, instead of FIFO mode.
  RMT.apb_conf.mem_tx_wrap_en = 1; // wrap around when hitting end of buffer
}



struct Esp_ws281x_LedChain* esp_ws281x_newChain(Esp_ws281x_LedType aLedType, unsigned int aGpioNo, unsigned int aMaxRetries)
{
  if (aLedType<0 || aLedType>=esp_num_ledtypes) return NULL; // invalid LED type
  // find free channel
  for (unsigned int rmtChannel=0; rmtChannel<NUM_RMT_CHANNELS; rmtChannel += channelspacing) {
    if (channelLEDChains[rmtChannel]==NULL) {
      // free channel found, use it
      Esp_ws281x_LedChain* newChain = malloc(sizeof(Esp_ws281x_LedChain));
      memset(newChain, 0, sizeof(Esp_ws281x_LedChain));
      newChain->ledType = aLedType;
      newChain->rmtChannel = rmtChannel;
      newChain->maxRetries = aMaxRetries;
      newChain->pulsesPerHalfBuffer = 32*channelspacing; // one RMT channel has 64 words of pulse memory
      newChain->leddata = NULL;
      newChain->pos = 0;
      newChain->len = 0;
      newChain->half = 0;
      newChain->retries = 0;
      // semaphore for locking buffer
      newChain->sem = xSemaphoreCreateBinary(); // semaphore is created taken...
      xSemaphoreGive(newChain->sem); // ...so to begin, give it, so  ws281x_setColors() can start sending
      // set up the pulse template from the led type
      LedTypeDescriptor_t* ltd = &ledTypeDescriptors[newChain->ledType];
      newChain->pulsebits[0].level0 = 1;
      newChain->pulsebits[0].level1 = 0;
      newChain->pulsebits[0].duration0 = PULSE_TO_RMTDELAY(ltd->T0Active_nS); // T1H
      newChain->pulsebits[0].duration1 = PULSE_TO_RMTDELAY(ltd->TPassive_min_nS+ltd->T0Active_nS); // longer than min passive to make 0 and 1 bits same duration
      newChain->pulsebits[1].level0 = 1;
      newChain->pulsebits[1].level1 = 0;
      newChain->pulsebits[1].duration0 = PULSE_TO_RMTDELAY(ltd->T0Active_nS*2); // assuming T1H = 2*T0H which is approximately correct for all types
      newChain->pulsebits[1].duration1 = PULSE_TO_RMTDELAY(ltd->TPassive_min_nS); // min passive time must be met in for 0 bits
      newChain->resetDuration = PULSE_TO_RMTDELAY(ltd->TReset_nS);
      #if TIMING_DEBUG
      newChain->timeOfLastLoad = 0;
      newChain->minReloadTime = 1000000; // should be below one second
      newChain->maxReloadTime = 0;
      newChain->totalRetries = 0;
      newChain->totalErrors = 0;
      #endif
      // store ptr to the chain
      channelLEDChains[rmtChannel] = newChain;
      // set up the output
      rmt_set_pin((rmt_channel_t)rmtChannel, RMT_MODE_TX, (gpio_num_t)aGpioNo);
      // set up the RMT channel parameters
      RMT.conf_ch[rmtChannel].conf0.div_cnt = DIVIDER;
      RMT.conf_ch[rmtChannel].conf0.mem_size = channelspacing; // how many mem blocks the channel can use (1..8 depending on max number of channels)
      RMT.conf_ch[rmtChannel].conf0.carrier_en = 0;
      RMT.conf_ch[rmtChannel].conf0.carrier_out_lv = 1;
      RMT.conf_ch[rmtChannel].conf0.mem_pd = 0;
      RMT.conf_ch[rmtChannel].conf1.rx_en = 0;
      RMT.conf_ch[rmtChannel].conf1.mem_owner = 0;
      RMT.conf_ch[rmtChannel].conf1.tx_conti_mode = 0;    //loop back mode.
      RMT.conf_ch[rmtChannel].conf1.ref_always_on = 1;    // use apb clock: 80M
      RMT.conf_ch[rmtChannel].conf1.idle_out_en = 1;
      RMT.conf_ch[rmtChannel].conf1.idle_out_lv = 0;
      RMT.tx_lim_ch[rmtChannel].limit = newChain->pulsesPerHalfBuffer;
      // enable interrupts for this channel
      RMT.int_ena.val |=
        (1<<(24+rmtChannel)) | // TX_THR enable (bits 24..31 for channels 0..7)
        (1<<(3*rmtChannel)); // TX_END enable (bits 0,3,6,9... for channels 0,1,2,3...)
      // ready now
      return newChain;
    }
  }
  // cannot add another chain
  return NULL;
}


void esp_ws281x_freeChain(struct Esp_ws281x_LedChain* aLedChain)
{
  if (xSemaphoreTake(aLedChain->sem, portMAX_DELAY)) {
    int rmtChannel = aLedChain->rmtChannel;
    // disable interrupts
    RMT.int_ena.val &= ~(
      (1<<(24+rmtChannel)) | // TX_THR enable (bits 24..31 for channels 0..7)
      (1<<(3*rmtChannel))
    ); // TX_END enable (bits 0,3,6,9... for channels 0,1,2,3...)
    // remove semaphore
    vSemaphoreDelete(aLedChain->sem);
    // remove from global array
    channelLEDChains[rmtChannel] = NULL;
    // free the chain data block
    free(aLedChain);
  }
}


void esp_ws281x_setColors(struct Esp_ws281x_LedChain* aLedChain, unsigned int aLenght, Esp_ws281x_pixel *aPixels)
{
  unsigned int i;

  if (xSemaphoreTake(aLedChain->sem, 0)) {
    #if TIMING_DEBUG
    if (aLedChain->timeOfLastLoad>0) {
      ESP_LOGI(TAG,
        "pulsesPerHalfBuffer=%d, reload time = %lld..%lld uS, retries=%d, totalRetries=%d, totalErrors=%d",
        aLedChain->pulsesPerHalfBuffer,
        aLedChain->minReloadTime, aLedChain->maxReloadTime,
        aLedChain->retries, aLedChain->totalRetries, aLedChain->totalErrors
      );
    }
    aLedChain->minReloadTime = 1000000; // should be below one second
    aLedChain->maxReloadTime = 0;
    #endif
    aLedChain->retries = 0;
    // ready for new data
    // - create output buffer
    LedTypeDescriptor_t *ltd = &ledTypeDescriptors[aLedChain->ledType];
    aLedChain->len = (aLenght * ltd->channels) * sizeof(uint8_t);
    aLedChain->leddata = malloc(aLedChain->len);
    // - fill output buffer
    for (i = 0; i < aLenght; i++) {
      int pbase = i*ltd->channels;
      aLedChain->leddata[pbase + ltd->fetchIdx[0]] =  aPixels[i].r;
      aLedChain->leddata[pbase + ltd->fetchIdx[1]] =  aPixels[i].g;
      aLedChain->leddata[pbase + ltd->fetchIdx[2]] =  aPixels[i].b;
      if (ltd->channels>3) {
        aLedChain->leddata[pbase + ltd->fetchIdx[3]] =  aPixels[i].w;
      }
    }
    // - start transferring the leddata
    start_transfer(aLedChain);
  }
  else {
    ESP_LOGW(TAG, "ws281x_setColors called again too soon");
  }
  return;
}
