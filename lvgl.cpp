//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2019-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44utils is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44utils is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44utils. If not, see <http://www.gnu.org/licenses/>.
//

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "lvgl.hpp"

#if ENABLE_LVGL

#include "application.hpp"

#if ENABLE_IMAGE_SUPPORT
  #include <png.h>
#endif

using namespace p44;

LvGL::LvGL() :
  mDisplay(nullptr),
  mWithKeyboard(false)
{
}


LvGL::~LvGL()
{
}


static LvGLPtr lvglP;

LvGL& LvGL::lvgl()
{
  if (!lvglP) {
    lvglP = new LvGL;
  }
  return *lvglP;
}

// MARK: - logging

#if LV_USE_LOG

extern "C" void lvgl_log_cb(lv_log_level_t aLevel, const char *aMsg)
{
  int logLevel = LOG_WARNING;
  switch (aLevel) {
    case LV_LOG_LEVEL_TRACE: logLevel = LOG_DEBUG; break; // A lot of logs to give detailed information
    case LV_LOG_LEVEL_INFO: logLevel = LOG_INFO; break; // Log important events
    case LV_LOG_LEVEL_WARN: logLevel = LOG_WARNING; break; // Log if something unwanted happened but didn't caused problem
    case LV_LOG_LEVEL_ERROR: logLevel = LOG_ERR; break; // Only critical issue, when the system may fail
  }
  POLOG(lvglP, logLevel, "%s", aMsg);
}

#endif // LV_USE_LOG


// MARK: - readonly file system for images

#if LV_USE_FILESYSTEM

typedef int pf_file_t; ///< platform file type

/// Open a native platform (posix) file
/// @param drv pointer to the current driver
/// @param file_p pointer to a file descriptor
/// @param fn name of the file.
/// @param mode element of 'fs_mode_t' enum or its 'OR' connection (e.g. FS_MODE_WR | FS_MODE_RD)
/// @return LV_FS_RES_OK: no error, the file is opened
///         any error from lv_fs_res_t enum
static lv_fs_res_t pf_open(lv_fs_drv_t*, void* file_p, const char* fn, lv_fs_mode_t mode)
{
  errno = 0;

  // make path relative to resource path
  string path = Application::sharedApplication()->resourcePath(fn);
  // read only
  if (mode!=LV_FS_MODE_RD) return LV_FS_RES_DENIED;
  pf_file_t fd = open(path.c_str(), O_RDONLY);
  if (fd<0) return LV_FS_RES_UNKNOWN;
  lseek(fd, 0, SEEK_SET);
  *((pf_file_t *)file_p) = fd;
  return LV_FS_RES_OK;
}


/// Close an opened file
/// @param drv pointer to the current driver
/// @param file_p pointer to a file descriptor
/// @return LV_FS_RES_OK: no error, the file is read
///         any error from lv__fs_res_t enum
static lv_fs_res_t pf_close(lv_fs_drv_t*, void *file_p)
{
  pf_file_t fd = *((pf_file_t *)file_p);
  close(fd);
  return LV_FS_RES_OK;
}


/// Read data from an opened file
/// @param drv pointer to the current driver
/// @param file_p pointer to a file descriptor
/// @param buf pointer to a memory block where to store the read data
/// @param btr number of Bytes To Read
/// @param br the real number of read bytes (Byte Read)
/// @return LV_FS_RES_OK: no error, the file is read
///         any error from lv__fs_res_t enum
static lv_fs_res_t pf_read(lv_fs_drv_t*, void* file_p, void *buf, uint32_t btr, uint32_t *br)
{
  pf_file_t fd = *((pf_file_t *)file_p);
  ssize_t by = read(fd, buf, btr);
  if (by<0) return LV_FS_RES_UNKNOWN;
  *br = (uint32_t)by;
  return LV_FS_RES_OK;
}

/// Set the read write pointer. Also expand the file size if necessary.
/// @param drv pointer to the current driver
/// @param file_p pointer to a file descriptor
/// @param pos the new position of read write pointer
/// @return LV_FS_RES_OK: no error, the file is read
///         any error from lv__fs_res_t enum
static lv_fs_res_t pf_seek(lv_fs_drv_t*, void* file_p, uint32_t pos)
{
  pf_file_t fd = *((pf_file_t *)file_p);
  off_t off = lseek(fd, pos, SEEK_SET);
  return off<0 ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

/// Give the position of the read write pointer
/// @param drv pointer to the current driver
/// @param file_p pointer to a file descriptor
/// @param pos_p pointer to to store the result
/// @return LV_FS_RES_OK: no error, the file is read
///         any error from lv__fs_res_t enum
static lv_fs_res_t pf_tell(lv_fs_drv_t*, void* file_p, uint32_t* pos_p)
{
  pf_file_t fd = *((pf_file_t *)file_p);
  off_t off = lseek(fd, 0, SEEK_CUR);
  if (off<0) return LV_FS_RES_UNKNOWN;
  *pos_p = (uint32_t)off;
  return LV_FS_RES_OK;
}

#endif // LV_USE_FILESYSTEM


#if ENABLE_IMAGE_SUPPORT

// MARK: - PNG image decoder

typedef struct {
  const void *src; /// to detect inconsistent png_decoder_info/png_decoder_open sequences
  png_image pngImage; /// control struct
  png_bytep pngBuffer; /// byte buffer
} PngDecoderState;



/// Get info about a PNG image
/// @param decoder pointer to the decoder where this function belongs
/// @param src can be file name or pointer to a C array
/// @param header store the info here
/// @return LV_RES_OK: no error; LV_RES_INV: can't get the info
static lv_res_t png_decoder_info(lv_img_decoder_t* decoder, const void* src, lv_image_header_t* header)
{
  lv_img_src_t imgtype = lv_img_src_get_type(src);
  if (imgtype==LV_IMAGE_SRC_SYMBOL) return LV_RES_INV; // short cut any PNG specifics
  // maintain a PngDecoderState as user data of the decoder
  PngDecoderState* pngDecP = (PngDecoderState*)decoder->user_data;
  if (pngDecP==NULL) {
    // create new decoder state
    pngDecP = new PngDecoderState;
    memset(&pngDecP->pngImage, 0, sizeof(png_image));
    pngDecP->pngImage.version = PNG_IMAGE_VERSION;
    pngDecP->pngBuffer = NULL;
    decoder->user_data = pngDecP;
  }
  else {
    // reset png image
    png_image_free(&pngDecP->pngImage);
  }
  if (pngDecP->pngBuffer!=NULL) {
    free(pngDecP->pngBuffer);
    pngDecP->pngBuffer = NULL;
  }
  if (imgtype==LV_IMAGE_SRC_FILE) {
    const char *fn = (const char*)src;
    size_t n = strlen(fn);
    if(n<5 || strcmp(&fn[strlen(fn) - 4], ".png")!=0) {
      // not a png file
      return LV_RES_INV;
    }
    // valid PNG filename, try to load PNG from there
    if (png_image_begin_read_from_file(&pngDecP->pngImage, (const char *)src) == 0) {
      // does not seem to be a PNG
      LOG(LOG_WARNING, "Cannot read PNG file %s: error: %s", fn, pngDecP->pngImage.message);
      return LV_RES_INV;
    }
  }
  else if (imgtype==LV_IMAGE_SRC_UNKNOWN) {
    // unknown by littlevGL, could be pointer to a PNG in memory
    size_t size = 100;
    if (png_image_begin_read_from_memory(&pngDecP->pngImage, src, size) == 0) {
      // does not seem to be a PNG
      LOG(LOG_WARNING, "Memory data is not a PNG: error: %s", pngDecP->pngImage.message);
      return LV_RES_INV;
    }
  }
  else {
    // unknown by this decoder (built-in decoder might recognize SYMBOL or VARIABLE
    return LV_RES_INV;
  }
  // if we get here, pngImage has basic info
  pngDecP->src = src; // save source pointer we got this info from
  header->cf = LV_IMG_CF_RAW_ALPHA; // PNGs have alpha
  header->w = pngDecP->pngImage.width;
  header->h = pngDecP->pngImage.height;
  return LV_RES_OK;
}


/// If the display is not in 32 bit format (ARGB888) then covert the image to the current color depth
/// @param img the lv_color32_t image (BGRA byte order), will be realloc()ed to fit new size if needed
/// @param px_cnt number of pixels in `img`
static void convert_color_depth(uint8_t* &img, uint32_t px_cnt)
{
  #if LV_COLOR_DEPTH == 16
  lv_color32_t* img_argb = (lv_color32_t*)img;
  lv_color_t c;
  uint32_t i;
  for(i = 0; i < px_cnt; i++) {
    c = LV_COLOR_MAKE(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
    img[i*3 + 2] = img_argb[i].ch.alpha;
    img[i*3 + 1] = c.full >> 8;
    img[i*3 + 0] = c.full & 0xFF;
  }
  img = (uint8_t*)realloc(img, px_cnt*3); // 2 bytes plus alpha
  #elif LV_COLOR_DEPTH == 8
  #error does not work yet, renders striped image
  lv_color32_t* img_argb = (lv_color32_t*)img;
  lv_color_t c;
  uint32_t i;
  for(i = 0; i < px_cnt; i++) {
    c = LV_COLOR_MAKE(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
    img[i*3 + 1] = img_argb[i].ch.alpha;
    img[i*3 + 0] = c.full;
  }
  img = (uint8_t*)realloc(img, px_cnt*2); // 1 byte color plus alpha
  #endif
}


/// Open a PNG image and return the decoded image
/// @param decoder pointer to the decoder where this function belongs
/// @param dsc pointer to a descriptor which describes this decoding session
/// @return LV_RES_OK: no error; LV_RES_INV: can't get the info
static lv_res_t png_decoder_open(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
  PngDecoderState* pngDecP = (PngDecoderState*)decoder->user_data;
  if (pngDecP==NULL || dsc->src!=pngDecP->src) {
    // apparently, png_decoder_info() was not called for this src before. Do it now, it will init the decoder state correctly
    lv_res_t res = png_decoder_info(decoder, dsc->src, &dsc->header);
    if (res!=LV_RES_OK) return res;
  }
  // now we have a valid PngDecoderState with the PNG read already successfully begun
  // - have libpng get the data in the lv_color32_t order, which is BGRA
  pngDecP->pngImage.format = PNG_FORMAT_BGRA;
  uint32_t pixCount = pngDecP->pngImage.height*pngDecP->pngImage.width;
  FOCUSLOG("Image width = %d", pngDecP->pngImage.width);
  FOCUSLOG("Image height = %d", pngDecP->pngImage.height);
  FOCUSLOG("Image width*height = pixCount = %d", pixCount);
  // - allocate enough memory to hold the image in this format; the
  //   PNG_IMAGE_SIZE macro uses the information about the image (width,
  //   height and format) stored in 'pngImage'.
  size_t imgSize = PNG_IMAGE_SIZE(pngDecP->pngImage);
  FOCUSLOG("Image size in bytes = %zu", imgSize);
  png_bytep imgData = (png_bytep)malloc(imgSize);
  if (!imgData) {
    LOG(LOG_WARNING, "Cannot allocate %zu bytes for PNG", imgSize);
    return LV_RES_INV;
  }
  // - now actually read the image
  if (png_image_finish_read(
    &pngDecP->pngImage,
    NULL, // background
    (png_bytep)imgData,
    0, // row_stride
    NULL //colormap
  ) == 0) {
    // error
    LOG(LOG_WARNING, "Error decoding PNG: error: %s", pngDecP->pngImage.message);
    free(imgData);
    return LV_RES_INV;
  }
  // - convert if LV_COLOR_DEPTH is not 32
  convert_color_depth(imgData, pixCount); // may realloc the buffer in attempt to reduce it
  if (imgData==NULL) return LV_RES_INV; // safety, should never happen as image gets REDUCED, not expanded!
  // - successfully read
  dsc->img_data = imgData;
  dsc->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  return LV_RES_OK;
}





/**
 * Decode `len` pixels starting from the given `x`, `y` coordinates and store them in `buf`.
 * Required only if the "open" function can't open the whole decoded pixel array. (dsc->img_data == NULL)
 * @param decoder pointer to the decoder the function associated with
 * @param dsc pointer to decoder descriptor
 * @param x start x coordinate
 * @param y start y coordinate
 * @param len number of pixels to decode
 * @param buf a buffer to store the decoded pixels
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 */
lv_res_t png_decoder_built_in_read_line(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc, lv_coord_t x,
                                    lv_coord_t y, lv_coord_t len, uint8_t * buf)
{
  /*With PNG it's usually not required*/

  /*Copy `len` pixels from `x` and `y` coordinates in True color format to `buf` */

  return LV_RES_OK;
}

/**
 * Free the allocated resources
 * @param decoder pointer to the decoder where this function belongs
 * @param dsc pointer to a descriptor which describes this decoding session
 */
static void png_decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
  // free stuff we put into userdata
  if (decoder->user_data) {
    PngDecoderState* pngDecP = (PngDecoderState*)decoder->user_data;
    png_image_free(&pngDecP->pngImage);
    if (pngDecP->pngBuffer) {
      free(pngDecP->pngBuffer);
      pngDecP->pngBuffer = NULL;
    }
    delete pngDecP;
    decoder->user_data = NULL;
  }
  // Call the built-in close function if the built-in open/read_line was used (which use user_data)
  //lv_img_decoder_built_in_close(decoder, dsc);
}

#endif // ENABLE_IMAGE_SUPPORT


// MARK: - get current millis

static inline uint32_t getmillis(void)
{
  return (uint32_t)_p44_millis();
}



// MARK: - littlevGL initialisation

void LvGL::init(const string aDispSpec)
{
  // defaults
  string dispdev = "/dev/fb0";
  int colorformat = 0;
  int32_t dx = 0; // default
  int32_t dy = 0; // default
  mWithKeyboard = false;
  lv_display_rotation_t rotation = LV_DISPLAY_ROTATION_0; // default
  string evdev = "/dev/input/event0";
  // aDispSpec:
  //   [<display device>[:<evdev device>]][<dx>:<dy>[:<colorformat>]][:<options>]
  //   - display device name, defaults to /dev/fb0, irrelevant for SDL sim
  //   - dx,dy: integers
  //   - options: characters
  //     - C: show cursor
  //     - L: rotate left
  //     - R: rotate right
  //     - U: upside down
  string part;
  const char *p = aDispSpec.c_str();
  int nmbrcnt = 0;
  int txtcnt = 0;
  while (nextPart(p, part, ':')) {
    if (!isdigit(part[0])) {
      // text
      if (nmbrcnt==0) {
        // texts before first number
        if (txtcnt==0) {
          dispdev = part;
          txtcnt++;
        }
        else if (txtcnt==1) {
          evdev = part;
          txtcnt++;
        }
      }
      else {
        // text after first number are options
        for (size_t i=0; i<part.size(); i++) {
          switch (part[i]) {
            case 'K': mWithKeyboard = true; break;
            case 'R': rotation = LV_DISPLAY_ROTATION_90; break;
            case 'U': rotation = LV_DISPLAY_ROTATION_180; break;
            case 'L': rotation = LV_DISPLAY_ROTATION_270; break;
          }
        }
      }
    }
    else {
      // number
      int n = atoi(part.c_str());
      switch (nmbrcnt) {
        case 0: dx = n; break;
        case 1: dy = n; break;
        default: break;
      }
      nmbrcnt++;
    }
  }
  // init library
  lv_init();
  #if LV_USE_LOG
  lv_log_register_print_cb(lvgl_log_cb);
  #endif
  // init tick getter
  lv_tick_set_cb(getmillis);
  // init display
  #if defined(__APPLE__)
  // - SDL2
  if (dx<=0) dx = 720;
  if (dy<=0) dy = 720;
  mDisplay = lv_sdl_window_create(dx, dy);
  #else
  // - Linux frame buffer
  mDisplay = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(mDisplay, dispdev.c_str()); // will read fb properties from device
  #endif
  if (dx>0 && dy>0) {
    // manual resolution
    lv_display_set_resolution(mDisplay, dx, dy);
  }
  if (colorformat>0) {
    // manual color format
    lv_display_set_color_format(mDisplay, (lv_color_format_t)colorformat);
  }
  if (rotation!=LV_DISPLAY_ROTATION_0) {
    lv_display_set_rotation(mDisplay, rotation);
  }
  // init input devices
  #if defined(__APPLE__)
  lv_indev_t *touch = lv_sdl_mouse_create();
  lv_indev_set_display(touch, mDisplay);
  if (mWithKeyboard) {
    lv_indev_t *kbd = lv_sdl_keyboard_create();
    lv_indev_set_display(kbd, mDisplay);
  }
  #else
  lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, evdev.c_str());
  lv_indev_set_display(touch, mDisplay);
  #endif
  // - register (readonly, only for getting images) file system support
  #if LV_USE_FILESYSTEM
  memset(&pf_fs_drv, 0, sizeof(lv_fs_drv_t));    // Initialization
  pf_fs_drv.file_size = sizeof(pf_file_t);       // Set up fields
  pf_fs_drv.letter = 'P';
  pf_fs_drv.open_cb = pf_open;
  pf_fs_drv.close_cb = pf_close;
  pf_fs_drv.read_cb = pf_read;
  pf_fs_drv.seek_cb = pf_seek;
  pf_fs_drv.tell_cb = pf_tell;
  lv_fs_drv_register(&pf_fs_drv);
  #endif
  #if ENABLE_IMAGE_SUPPORT
  // - PNG Image decoder
  lv_img_decoder_t * dec = lv_img_decoder_create();
  lv_img_decoder_set_info_cb(dec, png_decoder_info);
  lv_img_decoder_set_open_cb(dec, png_decoder_open);
  lv_img_decoder_set_close_cb(dec, png_decoder_close);
  #endif // ENABLE_IMAGE_SUPPORT
  // - schedule updates
  mLvglTicket.executeOnce(boost::bind(&LvGL::lvglTask, this, _1, _2));
}


#define LVGL_TICK_PERIOD (5*MilliSecond)

#if !LV_TICK_CUSTOM
  #warning LV_TICK_CUSTOM must be set, p44::LvGL does not call lv_tick_inc
#endif


void LvGL::lvglTask(MLTimer &aTimer, MLMicroSeconds aNow)
{
  lv_task_handler();
  #if defined(__APPLE__)
  // also need to update SDL2
  //monitor_sdl_refr_core();
  if (mDisplay) lv_refr_now(mDisplay);
  #endif
  if (mTaskCallback && mDisplay) {
    mTaskCallback();
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, LVGL_TICK_PERIOD);
}


void LvGL::setTaskCallback(SimpleCB aCallback)
{
  mTaskCallback = aCallback;
}




#endif // ENABLE_LVGL
