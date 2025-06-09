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

#include "lvglui.hpp"
#include "application.hpp"

using namespace p44;

#if ENABLE_LVGL

#if ENABLE_LVGLUI_SCRIPT_FUNCS
using namespace P44Script;
#endif


// MARK: - static utilities


static const char* getSymbolByName(const string aSymbolName)
{
  if (aSymbolName=="audio") return LV_SYMBOL_AUDIO;
  if (aSymbolName=="video") return LV_SYMBOL_VIDEO;
  if (aSymbolName=="list") return LV_SYMBOL_LIST;
  if (aSymbolName=="ok") return LV_SYMBOL_OK;
  if (aSymbolName=="close") return LV_SYMBOL_CLOSE;
  if (aSymbolName=="power") return LV_SYMBOL_POWER;
  if (aSymbolName=="settings") return LV_SYMBOL_SETTINGS;
  if (aSymbolName=="trash") return LV_SYMBOL_TRASH;
  if (aSymbolName=="home") return LV_SYMBOL_HOME;
  if (aSymbolName=="download") return LV_SYMBOL_DOWNLOAD;
  if (aSymbolName=="drive") return LV_SYMBOL_DRIVE;
  if (aSymbolName=="refresh") return LV_SYMBOL_REFRESH;
  if (aSymbolName=="mute") return LV_SYMBOL_MUTE;
  if (aSymbolName=="volume_mid") return LV_SYMBOL_VOLUME_MID;
  if (aSymbolName=="volume_max") return LV_SYMBOL_VOLUME_MAX;
  if (aSymbolName=="image") return LV_SYMBOL_IMAGE;
  if (aSymbolName=="edit") return LV_SYMBOL_EDIT;
  if (aSymbolName=="prev") return LV_SYMBOL_PREV;
  if (aSymbolName=="play") return LV_SYMBOL_PLAY;
  if (aSymbolName=="pause") return LV_SYMBOL_PAUSE;
  if (aSymbolName=="stop") return LV_SYMBOL_STOP;
  if (aSymbolName=="next") return LV_SYMBOL_NEXT;
  if (aSymbolName=="eject") return LV_SYMBOL_EJECT;
  if (aSymbolName=="left") return LV_SYMBOL_LEFT;
  if (aSymbolName=="right") return LV_SYMBOL_RIGHT;
  if (aSymbolName=="plus") return LV_SYMBOL_PLUS;
  if (aSymbolName=="minus") return LV_SYMBOL_MINUS;
  if (aSymbolName=="eye_open") return LV_SYMBOL_EYE_OPEN;
  if (aSymbolName=="eye_close") return LV_SYMBOL_EYE_CLOSE;
  if (aSymbolName=="warning") return LV_SYMBOL_WARNING;
  if (aSymbolName=="shuffle") return LV_SYMBOL_SHUFFLE;
  if (aSymbolName=="up") return LV_SYMBOL_UP;
  if (aSymbolName=="down") return LV_SYMBOL_DOWN;
  if (aSymbolName=="loop") return LV_SYMBOL_LOOP;
  if (aSymbolName=="directory") return LV_SYMBOL_DIRECTORY;
  if (aSymbolName=="upload") return LV_SYMBOL_UPLOAD;
  if (aSymbolName=="call") return LV_SYMBOL_CALL;
  if (aSymbolName=="cut") return LV_SYMBOL_CUT;
  if (aSymbolName=="copy") return LV_SYMBOL_COPY;
  if (aSymbolName=="save") return LV_SYMBOL_SAVE;
  if (aSymbolName=="charge") return LV_SYMBOL_CHARGE;
  if (aSymbolName=="paste") return LV_SYMBOL_PASTE;
  if (aSymbolName=="bell") return LV_SYMBOL_BELL;
  if (aSymbolName=="keyboard") return LV_SYMBOL_KEYBOARD;
  if (aSymbolName=="gps") return LV_SYMBOL_GPS;
  if (aSymbolName=="file") return LV_SYMBOL_FILE;
  if (aSymbolName=="wifi") return LV_SYMBOL_WIFI;
  if (aSymbolName=="battery_full") return LV_SYMBOL_BATTERY_FULL;
  if (aSymbolName=="battery_3") return LV_SYMBOL_BATTERY_3;
  if (aSymbolName=="battery_2") return LV_SYMBOL_BATTERY_2;
  if (aSymbolName=="battery_1") return LV_SYMBOL_BATTERY_1;
  if (aSymbolName=="battery_empty") return LV_SYMBOL_BATTERY_EMPTY;
  if (aSymbolName=="usb") return LV_SYMBOL_USB;
  if (aSymbolName=="bluetooth") return LV_SYMBOL_BLUETOOTH;
  if (aSymbolName=="backspace") return LV_SYMBOL_BACKSPACE;
  if (aSymbolName=="sd_card") return LV_SYMBOL_SD_CARD;
  if (aSymbolName=="new_line") return LV_SYMBOL_NEW_LINE;
  return "";
}




static lv_state_t getStateByName(const string aState)
{
  // states
  if (aState=="checked") return LV_STATE_CHECKED;
  if (aState=="focused") return LV_STATE_FOCUSED;
  if (aState=="keyfocused") return LV_STATE_FOCUS_KEY;
  if (aState=="edited") return LV_STATE_EDITED;
  if (aState=="hovered") return LV_STATE_HOVERED;
  if (aState=="pressed") return LV_STATE_PRESSED;
  if (aState=="scrolled") return LV_STATE_SCROLLED;
  if (aState=="disabled") return LV_STATE_DISABLED;
  if (aState=="user1") return LV_STATE_USER_1;
  if (aState=="user2") return LV_STATE_USER_2;
  if (aState=="user3") return LV_STATE_USER_3;
  if (aState=="user4") return LV_STATE_USER_4;
  return LV_STATE_DEFAULT;
}


static lv_style_selector_t getSelectorByList(const string aStateList)
{
  const char* p = aStateList.c_str();
  string part;
  lv_style_selector_t selector = LV_STATE_DEFAULT;
  while (nextPart(p, part, '|')) {
    // states
    lv_state_t state = getStateByName(part);
    if (state!=LV_STATE_DEFAULT) {
      selector |= state;
    }
    else {
      // parts
      if (part=="main") selector |= LV_PART_MAIN;
      else if (part=="scrollbar") selector |= LV_PART_SCROLLBAR;
      else if (part=="indicator") selector |= LV_PART_INDICATOR;
      else if (part=="knob") selector |= LV_PART_KNOB;
      else if (part=="selected") selector |= LV_PART_SELECTED;
      else if (part=="items") selector |= LV_PART_ITEMS;
      else if (part=="cursor") selector |= LV_PART_CURSOR;
      else if (part=="any") selector |= LV_PART_ANY;
    }
  }
  return selector;
}


static void getStateChangesByList(const string aStatesList, uint16_t& aStatesToSet, uint16_t& aStatesToClear)
{
  const char* p = aStatesList.c_str();
  string part;
  aStatesToSet = 0;
  aStatesToClear = 0;
  while (nextPart(p, part, ',')) {
    bool del = false;
    if (part[0]=='-') { del = true; part.erase(0,1); }
    else if (part[0]=='+') { part.erase(0,1); }
    lv_state_t state = getStateByName(part);
    if (del) aStatesToSet |= state;
    else aStatesToClear |= state;
  }
}



static void getFlagChangesByList(const string aFlagsList, uint32_t& aFlagsToSet, uint32_t& aFlagsToClear)
{
  const char* p = aFlagsList.c_str();
  string part;
  aFlagsToSet = 0;
  aFlagsToClear = 0;
  while (nextPart(p, part, ',')) {
    bool del = false;
    uint32_t flag = 0;
    if (part[0]=='-') { del = true; part.erase(0,1); }
    else if (part[0]=='+') { part.erase(0,1); }
    if (part=="hidden") flag = LV_OBJ_FLAG_HIDDEN;
    else if (part=="clickable") flag = LV_OBJ_FLAG_CLICKABLE;
    else if (part=="focusable") flag = LV_OBJ_FLAG_CLICK_FOCUSABLE;
    else if (part=="checkable") flag = LV_OBJ_FLAG_CHECKABLE;
    else if (part=="scrollable") flag = LV_OBJ_FLAG_SCROLLABLE;
    else if (part=="elastic") flag = LV_OBJ_FLAG_SCROLL_ELASTIC;
    else if (part=="momentum") flag = LV_OBJ_FLAG_SCROLL_MOMENTUM;
    else if (part=="scroll_one") flag = LV_OBJ_FLAG_SCROLL_ONE;
    else if (part=="scroll_chain_x") flag = LV_OBJ_FLAG_SCROLL_CHAIN_HOR;
    else if (part=="scroll_chain_y") flag = LV_OBJ_FLAG_SCROLL_CHAIN_VER;
    else if (part=="scroll_chain") flag = LV_OBJ_FLAG_SCROLL_CHAIN;
    else if (part=="scroll_onfocus") flag = LV_OBJ_FLAG_SCROLL_ON_FOCUS;
    else if (part=="scroll_witharrow") flag = LV_OBJ_FLAG_SCROLL_WITH_ARROW;
    else if (part=="snappable") flag = LV_OBJ_FLAG_SNAPPABLE;
    else if (part=="presslock") flag = LV_OBJ_FLAG_PRESS_LOCK;
    else if (part=="eventbubble") flag = LV_OBJ_FLAG_EVENT_BUBBLE;
    else if (part=="gesturebubble") flag = LV_OBJ_FLAG_GESTURE_BUBBLE;
    else if (part=="hit_test") flag = LV_OBJ_FLAG_ADV_HITTEST;
    else if (part=="ignore_layout") flag = LV_OBJ_FLAG_IGNORE_LAYOUT;
    else if (part=="floating") flag = LV_OBJ_FLAG_FLOATING;
    else if (part=="drawtaskevents") flag = LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS;
    else if (part=="overflowvisible") flag = LV_OBJ_FLAG_OVERFLOW_VISIBLE;
    else if (part=="newtrack") flag = LV_OBJ_FLAG_FLEX_IN_NEW_TRACK;
    else if (part=="layout1") flag = LV_OBJ_FLAG_LAYOUT_1;
    else if (part=="layout2") flag = LV_OBJ_FLAG_LAYOUT_2;
    else if (part=="widget1") flag = LV_OBJ_FLAG_WIDGET_1;
    else if (part=="widget2") flag = LV_OBJ_FLAG_WIDGET_2;
    else if (part=="user1") flag = LV_OBJ_FLAG_USER_1;
    else if (part=="user2") flag = LV_OBJ_FLAG_USER_2;
    else if (part=="user3") flag = LV_OBJ_FLAG_USER_3;
    else if (part=="user4") flag = LV_OBJ_FLAG_USER_4;
    if (del) aFlagsToClear |= flag;
    else aFlagsToSet |= flag;
  }
}

// MARK: style properties


static ErrorPtr intPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  aStyleValue.num = aJsonValue->int32Value();
  return ErrorPtr();
}


static ErrorPtr coordPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  // strings might be values in other units than pixels
  lv_coord_t coord = 0;
  if (aJsonValue->isType(json_type_string)) {
    string s = aJsonValue->stringValue();
    int32_t v;
    char unit = 0;
    if (sscanf(s.c_str(), "%d%c", &v, &unit)>=1) {
      if (unit=='%') coord = lv_pct(v);
      else coord = v; // no other recognized units so far
    }
  }
  else {
    return intPropValue(aJsonValue, aStyleValue);
  }
  aStyleValue.num = coord;
  return ErrorPtr();
}


static ErrorPtr radiusPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  if (aJsonValue->stringValue()!="circle") return coordPropValue(aJsonValue, aStyleValue);
  aStyleValue.num = LV_RADIUS_CIRCLE;
  return ErrorPtr();
}


static ErrorPtr scalePropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  aStyleValue.num = aJsonValue->doubleValue()*256;
  return ErrorPtr();
}


static ErrorPtr anglePropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  aStyleValue.num = aJsonValue->doubleValue()*10;
  return ErrorPtr();
}


static ErrorPtr per255PropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  aStyleValue.num = aJsonValue->doubleValue()*2.55; // 0..100 -> 0..255
  return ErrorPtr();
}


static ErrorPtr boolPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  aStyleValue.num = aJsonValue->boolValue();
  return ErrorPtr();
}



static ErrorPtr alignPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string alignMode = aJsonValue->stringValue();
  const char *p = alignMode.c_str();
  bool in = true; // default to in
  bool top = false;
  bool mid = false;
  bool bottom = false;
  bool left = false;
  bool right = false;
  string tok;
  lv_align_t align;
  if (alignMode=="center") align = LV_ALIGN_CENTER; // special case not combinable from the elements below
  else {
    while (nextPart(p, tok, ',')) {
      if (tok=="top") top = true;
      else if (tok=="mid") mid = true;
      else if (tok=="bottom") bottom = true;
      else if (tok=="left") left = true;
      else if (tok=="right") right = true;
      else if (tok=="in") in = true;
      else if (tok=="out") in = false;
    }
    if (in && top && left) align = LV_ALIGN_TOP_LEFT;
    else if (in && top && mid) align = LV_ALIGN_TOP_MID;
    else if (in && top && right) align = LV_ALIGN_TOP_RIGHT;
    else if (in && bottom && left) align = LV_ALIGN_BOTTOM_LEFT;
    else if (in && bottom && mid) align = LV_ALIGN_BOTTOM_MID;
    else if (in && bottom && right) align = LV_ALIGN_BOTTOM_RIGHT;
    else if (in && left && mid) align = LV_ALIGN_LEFT_MID;
    else if (in && right && mid) align = LV_ALIGN_RIGHT_MID;
    else if (!in && top && left) align = LV_ALIGN_OUT_TOP_LEFT;
    else if (!in && top && mid) align = LV_ALIGN_OUT_TOP_MID;
    else if (!in && top && right) align = LV_ALIGN_OUT_TOP_RIGHT;
    else if (!in && bottom && left) align = LV_ALIGN_OUT_BOTTOM_LEFT;
    else if (!in && bottom && mid) align = LV_ALIGN_OUT_BOTTOM_MID;
    else if (!in && bottom && right) align = LV_ALIGN_OUT_BOTTOM_RIGHT;
    else if (!in && left && top) align = LV_ALIGN_OUT_LEFT_TOP;
    else if (!in && left && mid) align = LV_ALIGN_OUT_LEFT_MID;
    else if (!in && left && bottom) align = LV_ALIGN_OUT_LEFT_BOTTOM;
    else if (!in && right && top) align = LV_ALIGN_OUT_RIGHT_TOP;
    else if (!in && right && mid) align = LV_ALIGN_OUT_RIGHT_MID;
    else if (!in && right && bottom) align = LV_ALIGN_OUT_RIGHT_BOTTOM;
    else return TextError::err("unknown align mode '%s'", alignMode.c_str());
  }
  aStyleValue.num = align;
  return ErrorPtr();
}


static ErrorPtr fontPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string fontName = aJsonValue->stringValue();
  const lv_font_t* font = nullptr;
  if (false) {}
  #if LV_FONT_MONTSERRAT_8
  else if (fontName=="montserrat8") font = &lv_font_montserrat_8;
  #endif
  #if LV_FONT_MONTSERRAT_10
  else if (fontName=="montserrat10") font = &lv_font_montserrat_10;
  #endif
  #if LV_FONT_MONTSERRAT_12
  else if (fontName=="montserrat12") font = &lv_font_montserrat_12;
  #endif
  #if LV_FONT_MONTSERRAT_14
  else if (fontName=="montserrat14") font = &lv_font_montserrat_14;
  #endif
  #if LV_FONT_MONTSERRAT_16
  else if (fontName=="montserrat16") font = &lv_font_montserrat_16;
  #endif
  #if LV_FONT_MONTSERRAT_18
  else if (fontName=="montserrat18") font = &lv_font_montserrat_18;
  #endif
  #if LV_FONT_MONTSERRAT_20
  else if (fontName=="montserrat20") font = &lv_font_montserrat_20;
  #endif
  #if LV_FONT_MONTSERRAT_22
  else if (fontName=="montserrat22") font = &lv_font_montserrat_22;
  #endif
  #if LV_FONT_MONTSERRAT_24
  else if (fontName=="montserrat24") font = &lv_font_montserrat_24;
  #endif
  #if LV_FONT_MONTSERRAT_26
  else if (fontName=="montserrat26") font = &lv_font_montserrat_26;
  #endif
  #if LV_FONT_MONTSERRAT_28
  else if (fontName=="montserrat28") font = &lv_font_montserrat_28;
  #endif
  #if LV_FONT_MONTSERRAT_30
  else if (fontName=="montserrat30") font = &lv_font_montserrat_30;
  #endif
  #if LV_FONT_MONTSERRAT_32
  else if (fontName=="montserrat32") font = &lv_font_montserrat_32;
  #endif
  #if LV_FONT_MONTSERRAT_34
  else if (fontName=="montserrat34") font = &lv_font_montserrat_34;
  #endif
  #if LV_FONT_MONTSERRAT_36
  else if (fontName=="montserrat36") font = &lv_font_montserrat_36;
  #endif
  #if LV_FONT_MONTSERRAT_38
  else if (fontName=="montserrat38") font = &lv_font_montserrat_38;
  #endif
  #if LV_FONT_MONTSERRAT_40
  else if (fontName=="montserrat40") font = &lv_font_montserrat_40;
  #endif
  #if LV_FONT_MONTSERRAT_42
  else if (fontName=="montserrat42") font = &lv_font_montserrat_42;
  #endif
  #if LV_FONT_MONTSERRAT_44
  else if (fontName=="montserrat44") font = &lv_font_montserrat_44;
  #endif
  #if LV_FONT_MONTSERRAT_46
  else if (fontName=="montserrat46") font = &lv_font_montserrat_46;
  #endif
  #if LV_FONT_MONTSERRAT_48
  else if (fontName=="montserrat48") font = &lv_font_montserrat_48;
  #endif
  else {
    return TextError::err("unknown font '%s'", fontName.c_str());
  }
  aStyleValue.ptr = font;
  return ErrorPtr();
}



static lv_palette_t getPaletteEntryFromColorSpec(const string aPaletteColor)
{
  if (aPaletteColor=="red") return LV_PALETTE_RED;
  if (aPaletteColor=="pink") return LV_PALETTE_PINK;
  if (aPaletteColor=="purple") return LV_PALETTE_PURPLE;
  if (aPaletteColor=="deeppurple") return LV_PALETTE_DEEP_PURPLE;
  if (aPaletteColor=="red") return LV_PALETTE_INDIGO;
  if (aPaletteColor=="blue") return LV_PALETTE_BLUE;
  if (aPaletteColor=="lightblue") return LV_PALETTE_LIGHT_BLUE;
  if (aPaletteColor=="cyan") return LV_PALETTE_CYAN;
  if (aPaletteColor=="teal") return LV_PALETTE_TEAL;
  if (aPaletteColor=="green") return LV_PALETTE_GREEN;
  if (aPaletteColor=="lightgreen") return LV_PALETTE_LIGHT_GREEN;
  if (aPaletteColor=="lime") return LV_PALETTE_LIME;
  if (aPaletteColor=="yellow") return LV_PALETTE_YELLOW;
  if (aPaletteColor=="amber") return LV_PALETTE_AMBER;
  if (aPaletteColor=="orange") return LV_PALETTE_ORANGE;
  if (aPaletteColor=="deeporange") return LV_PALETTE_DEEP_ORANGE;
  if (aPaletteColor=="brown") return LV_PALETTE_BROWN;
  if (aPaletteColor=="bluegrey" || aPaletteColor=="bluegray") return LV_PALETTE_BLUE_GREY;
  if (aPaletteColor=="grey" || aPaletteColor=="gray") return LV_PALETTE_GREY;
  return LV_PALETTE_NONE;
}


static ErrorPtr colorPropValue(JsonObjectPtr aJsonValue, bool& aHasColor, lv_style_value_t& aColorValue, bool& aHasOpacity, lv_style_value_t& aOpaValue)
{
  const string colorSpec = aJsonValue->stringValue();
  int r = 0, g = 0, b = 0;
  lv_opa_t a;
  aHasOpacity = false;
  aHasColor = true;
  size_t n = colorSpec.size();
  lv_color_t color;
  do {
    if (n>0) {
      if (colorSpec[0]!='#') {
        // check palette
        // syntax <palettecolorname>[+<lighten_level>|-<darken_level>]
        // - find adjustment
        size_t i = colorSpec.find_first_of("+-");
        int32_t adj = 0;
        if (i!=string::npos) {
          sscanf(colorSpec.c_str()+i, "%d", &adj);
        }
        else {
          i = colorSpec.size();
        }
        // - find color
        string colname = colorSpec.substr(0, i);
        if (colname=="black") { color = lv_color_black(); break; }
        else if (colname=="white") { color = lv_color_white(); break; }
        else if (colname=="transparent") {
          aHasOpacity = true;
          a = 0;
          color = lv_color_black();
          break;
        }
        lv_palette_t pi = getPaletteEntryFromColorSpec(colname);
        if (pi!=LV_PALETTE_NONE) {
          if (adj==0) color = lv_palette_main(pi);
          else if (adj>0) color = lv_palette_lighten(pi, adj);
          else color =lv_palette_darken(pi, -adj);
          break;
        }
        return TextError::err("unknown color '%s'", colorSpec.c_str());
      }
      else {
        // web color
        // syntax: #rrggbb or #rgb or #aarrggbb or #argb
        n--; // string size without # !
        uint32_t h;
        if (sscanf(colorSpec.c_str()+1, "%x", &h)==1) {
          if (n==1) {
            // alpha-only single digit
            a = h&0xF; a |= a<<4;
            aHasOpacity = true;
            aHasColor = false;
          }
          else if (n==2) {
            // alpha-only double digit
            a = h&0xFF;
            aHasOpacity = true;
            aHasColor = false;
          }
          else if (n<=4) {
            // short form RGB or ARGB
            if (n==4) { aHasOpacity = true; a = (h>>12)&0xF; a |= a<<4; }
            r = (h>>8)&0xF; r |= r<<4;
            g = (h>>4)&0xF; g |= g<<4;
            b = (h>>0)&0xF; b |= b<<4;
          }
          else {
            // long form RRGGBB or AARRGGBB
            a = 255;
            if (n==8) { aHasOpacity = true; a = (h>>24)&0xFF; }
            r = (h>>16)&0xFF;
            g = (h>>8)&0xFF;
            b = (h>>0)&0xFF;
          }
          color = lv_color_make(r,g,b);
        }
        else {
          return TextError::err("unknown color '%s'", colorSpec.c_str());
        }
      }
    }
  } while(false);
  if (aHasOpacity) aOpaValue.num = a;
  aColorValue.color = color;
  return ErrorPtr();
}


static ErrorPtr colorPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  lv_style_value_t opa;
  bool hasOpa, hasColor;
  ErrorPtr err = colorPropValue(aJsonValue, hasColor, aStyleValue, hasOpa, opa);
  if (Error::isOK(err)) {
    if (!hasColor) aStyleValue.color = lv_color_black();
  }
  return err;
}




static ErrorPtr gradientDirPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string gradSpec = aJsonValue->stringValue();
  lv_grad_dir_t grad;
  if (gradSpec=="vertical") grad = LV_GRAD_DIR_VER;
  else if (gradSpec=="horizontal") grad = LV_GRAD_DIR_HOR;
  else if (gradSpec=="linear") grad = LV_GRAD_DIR_LINEAR;
  else if (gradSpec=="radial") grad = LV_GRAD_DIR_RADIAL;
  else if (gradSpec=="conical") grad = LV_GRAD_DIR_CONICAL;
  else if (gradSpec=="none") grad = LV_GRAD_DIR_NONE;
  else {
    return TextError::err("unknown gradient direction '%s'", gradSpec.c_str());
  }
  aStyleValue.num = grad;
  return ErrorPtr();
}


static ErrorPtr borderSidesPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string s = aJsonValue->stringValue();
  const char* p = s.c_str();
  string part;
  uint8_t sides = LV_BORDER_SIDE_NONE;
  while (nextPart(p, part, ',')) {
    if (part=="bottom") sides |= LV_BORDER_SIDE_BOTTOM;
    else if (part=="top") sides |= LV_BORDER_SIDE_TOP;
    else if (part=="left") sides |= LV_BORDER_SIDE_LEFT;
    else if (part=="right") sides |= LV_BORDER_SIDE_RIGHT;
    else if (part=="all") sides |= LV_BORDER_SIDE_FULL;
    else if (part=="internal") sides |= LV_BORDER_SIDE_INTERNAL;
    else return TextError::err("unknown border part '%s'", part.c_str());
  }
  aStyleValue.num = sides;
  return ErrorPtr();
}


static ErrorPtr textDecorPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string s = aJsonValue->stringValue();
  const char* p = s.c_str();
  string part;
  uint8_t decor = LV_TEXT_DECOR_NONE;
  while (nextPart(p, part, ',')) {
    if (part=="underline") decor |= LV_TEXT_DECOR_UNDERLINE;
    else if (part=="strikethrough") decor |= LV_TEXT_DECOR_STRIKETHROUGH;
    else return TextError::err("unknown text decor '%s'", part.c_str());
  }
  aStyleValue.num = decor;
  return ErrorPtr();
}


static ErrorPtr textAlignPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string textAlign = aJsonValue->stringValue();
  lv_text_align_t align;
  if (textAlign=="left") align = LV_TEXT_ALIGN_LEFT;
  else if (textAlign=="center") align = LV_TEXT_ALIGN_CENTER;
  else if (textAlign=="right") align = LV_TEXT_ALIGN_RIGHT;
  else if (textAlign=="auto") align = LV_TEXT_ALIGN_AUTO;
  else return TextError::err("unknown text alignment '%s'", textAlign.c_str());
  aStyleValue.num = align;
  return ErrorPtr();
}


static ErrorPtr blendModePropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string blendMode = aJsonValue->stringValue();
  lv_blend_mode_t blend;
  if (blendMode=="additive") blend = LV_BLEND_MODE_ADDITIVE;
  else if (blendMode=="subtractive") blend = LV_BLEND_MODE_SUBTRACTIVE;
  else if (blendMode=="multiply") blend = LV_BLEND_MODE_MULTIPLY;
  else if (blendMode=="normal") blend = LV_BLEND_MODE_NORMAL;
  else return TextError::err("unknown blend mode '%s'", blendMode.c_str());
  aStyleValue.num = blend;
  return ErrorPtr();
}


static ErrorPtr layoutPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string layoutName = aJsonValue->stringValue();
  lv_layout_t layout;
  if (layoutName=="flex") layout = LV_LAYOUT_FLEX;
  else if (layoutName=="grid") layout = LV_LAYOUT_GRID;
  else if (layoutName=="none") layout = LV_LAYOUT_NONE;
  else return TextError::err("unknown layout '%s'", layoutName.c_str());
  aStyleValue.num = layout;
  return ErrorPtr();
}


static ErrorPtr flexAlignPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string flexAlign = aJsonValue->stringValue();
  lv_flex_align_t align;
  if (flexAlign=="start") align = LV_FLEX_ALIGN_START;
  else if (flexAlign=="end") align = LV_FLEX_ALIGN_END;
  else if (flexAlign=="evenly") align = LV_FLEX_ALIGN_SPACE_EVENLY;
  else if (flexAlign=="around") align = LV_FLEX_ALIGN_SPACE_AROUND;
  else if (flexAlign=="between") align = LV_FLEX_ALIGN_SPACE_BETWEEN;
  else if (flexAlign=="center") align = LV_FLEX_ALIGN_CENTER;
  else return TextError::err("unknown flex align '%s'", flexAlign.c_str());
  aStyleValue.num = align;
  return ErrorPtr();
}



static ErrorPtr flexFlowPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string flexFlow = aJsonValue->stringValue();
  const char* p = flexFlow.c_str();
  string part;
  uint8_t flow = LV_FLEX_FLOW_ROW;
  while (nextPart(p, part, ',')) {
    if (part=="column") flow |= LV_FLEX_COLUMN;
    else if (part=="wrap") flow |= LV_FLEX_WRAP;
    else if (part=="reverse") flow |= LV_FLEX_REVERSE;
    else if (part=="row");
    else return TextError::err("unknown flex flow '%s'", flexFlow.c_str());
  }
  aStyleValue.num = flow;
  return ErrorPtr();
}


static ErrorPtr gridAlignPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  const string gridAlign = aJsonValue->stringValue();
  lv_grid_align_t align;
  if (gridAlign=="start") align = LV_GRID_ALIGN_START;
  else if (gridAlign=="end") align = LV_GRID_ALIGN_END;
  else if (gridAlign=="evenly") align = LV_GRID_ALIGN_SPACE_EVENLY;
  else if (gridAlign=="around") align = LV_GRID_ALIGN_SPACE_AROUND;
  else if (gridAlign=="between") align = LV_GRID_ALIGN_SPACE_BETWEEN;
  else if (gridAlign=="center") align = LV_GRID_ALIGN_CENTER;
  else return TextError::err("unknown grid align '%s'", gridAlign.c_str());
  aStyleValue.num = align;
  return ErrorPtr();
}


static ErrorPtr gridTemplateArrayPropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  int n = aJsonValue->arrayLength();
  int32_t* aGridTemplateP = nullptr;
  if (n>0) {
    aGridTemplateP = new int32_t[n+1]; // plus terminator
    for (int i = 0; i<n; i++) {
      aGridTemplateP[i] = aJsonValue->arrayGet(i)->int32Value();
    }
    aGridTemplateP[n] = LV_GRID_TEMPLATE_LAST;
  }
  aStyleValue.ptr = aGridTemplateP;
  return ErrorPtr();
}


static ErrorPtr imagePropValue(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue)
{
  #warning %%% to implement
  return TextError::err("Image properties not yet implemented");
}


typedef ErrorPtr (*PropValueConverter)(JsonObjectPtr aJsonValue, lv_style_value_t& aStyleValue);

typedef struct {
  const char* propname;
  lv_style_prop_t propid;
  lv_style_prop_t opa_propid;
  PropValueConverter propconv;
} PropDef;

enum {
  P44_STYLE_PAD_ALL=LV_STYLE_LAST_BUILT_IN_PROP+1,
  P44_STYLE_MARGIN_ALL=LV_STYLE_LAST_BUILT_IN_PROP+2
};

static const PropDef propDefs[] = {
  { "x", LV_STYLE_X, 0, coordPropValue },
  { "y", LV_STYLE_Y, 0, coordPropValue },
  { "dx", LV_STYLE_WIDTH, 0, coordPropValue },
  { "min_dx", LV_STYLE_MIN_WIDTH, 0, coordPropValue },
  { "max_dx", LV_STYLE_MAX_WIDTH, 0, coordPropValue },
  { "dy", LV_STYLE_HEIGHT, 0, coordPropValue },
  { "min_dy", LV_STYLE_MIN_HEIGHT, 0, coordPropValue },
  { "max_dy", LV_STYLE_MAX_HEIGHT, 0, coordPropValue },
  { "length", LV_STYLE_LENGTH, 0, coordPropValue },
  { "align", LV_STYLE_LENGTH, 0, alignPropValue },
  { "transform_dx", LV_STYLE_TRANSFORM_WIDTH, 0, coordPropValue },
  { "transform_dy", LV_STYLE_TRANSFORM_HEIGHT, 0, coordPropValue },
  { "translate_x", LV_STYLE_TRANSLATE_X, 0, coordPropValue },
  { "translate_y", LV_STYLE_TRANSLATE_Y, 0, coordPropValue },
  { "scale_x", LV_STYLE_TRANSFORM_SCALE_X, 0, scalePropValue },
  { "scale_y", LV_STYLE_TRANSFORM_SCALE_Y, 0, scalePropValue },
  { "rotation", LV_STYLE_TRANSFORM_ROTATION, 0, anglePropValue },
  { "pivot_x", LV_STYLE_TRANSFORM_PIVOT_X, 0, coordPropValue },
  { "pivot_y", LV_STYLE_TRANSFORM_PIVOT_Y, 0, coordPropValue },
  { "skew_x", LV_STYLE_TRANSFORM_SKEW_X, 0, anglePropValue },
  { "skew_y", LV_STYLE_TRANSFORM_SKEW_Y, 0, anglePropValue },
  { "padding", P44_STYLE_PAD_ALL, 0, coordPropValue },
  { "padding_top", LV_STYLE_PAD_TOP, 0, coordPropValue },
  { "padding_bottom", LV_STYLE_PAD_BOTTOM, 0, coordPropValue },
  { "padding_left", LV_STYLE_PAD_LEFT, 0, coordPropValue },
  { "padding_right", LV_STYLE_PAD_RIGHT, 0, coordPropValue },
  { "padding_row", LV_STYLE_PAD_ROW, 0, coordPropValue },
  { "padding_column", LV_STYLE_PAD_COLUMN, 0, coordPropValue },
  { "margin", P44_STYLE_MARGIN_ALL, 0, coordPropValue },
  { "margin_top", LV_STYLE_MARGIN_TOP, 0, coordPropValue },
  { "margin_bottom", LV_STYLE_MARGIN_BOTTOM, 0, coordPropValue },
  { "margin_left", LV_STYLE_MARGIN_LEFT, 0, coordPropValue },
  { "margin_right", LV_STYLE_MARGIN_RIGHT, 0, coordPropValue },
  { "color", LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, nullptr },
  { "color_main", LV_STYLE_BG_COLOR, LV_STYLE_BG_MAIN_OPA, nullptr },
  { "gradient_color", LV_STYLE_BG_GRAD_COLOR, LV_STYLE_BG_GRAD_OPA, nullptr },
  { "gradient_dir", LV_STYLE_BG_GRAD_DIR, 0, gradientDirPropValue },
  { "gradient_start", LV_STYLE_BG_MAIN_STOP, 0, per255PropValue },
  { "gradient_stop", LV_STYLE_BG_GRAD_STOP, 0, per255PropValue },
  { "bg_image", LV_STYLE_BG_GRAD_STOP, 0, imagePropValue },
  { "bg_recoloring", LV_STYLE_BG_IMAGE_RECOLOR, LV_STYLE_BG_IMAGE_RECOLOR_OPA, nullptr },
  { "bg_tiled", LV_STYLE_BG_IMAGE_TILED, 0, boolPropValue },
  { "border_color", LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_OPA, nullptr },
  { "border_width", LV_STYLE_BORDER_WIDTH, 0, coordPropValue },
  { "border_sides", LV_STYLE_BORDER_SIDE, 0, borderSidesPropValue },
  { "border_post", LV_STYLE_BORDER_POST, 0, boolPropValue },
  { "outline_color", LV_STYLE_OUTLINE_COLOR, LV_STYLE_OUTLINE_OPA, nullptr },
  { "outline_width", LV_STYLE_OUTLINE_WIDTH, 0, coordPropValue },
  { "outline_pad", LV_STYLE_OUTLINE_PAD, 0, coordPropValue },
  { "shadow_color", LV_STYLE_SHADOW_COLOR, LV_STYLE_SHADOW_OPA, nullptr },
  { "shadow_width", LV_STYLE_SHADOW_WIDTH, 0, coordPropValue },
  { "shadow_dx", LV_STYLE_SHADOW_OFFSET_X, 0, coordPropValue },
  { "shadow_dy", LV_STYLE_SHADOW_OFFSET_Y, 0, coordPropValue },
  { "shadow_spread", LV_STYLE_SHADOW_OFFSET_Y, 0, coordPropValue },
  { "image_alpha", LV_STYLE_IMAGE_OPA, 0, intPropValue },
  { "image_recoloring", LV_STYLE_IMAGE_RECOLOR, LV_STYLE_IMAGE_RECOLOR_OPA, nullptr },
  { "line_color", LV_STYLE_LINE_COLOR, LV_STYLE_LINE_OPA, nullptr },
  { "line_width", LV_STYLE_LINE_WIDTH, 0, coordPropValue },
  { "line_dash", LV_STYLE_LINE_DASH_WIDTH, 0, coordPropValue },
  { "line_gap", LV_STYLE_LINE_DASH_GAP, 0, coordPropValue },
  { "line_rounded", LV_STYLE_LINE_ROUNDED, 0, boolPropValue },
  { "arc_color", LV_STYLE_ARC_COLOR, LV_STYLE_ARC_OPA, nullptr },
  { "arc_width", LV_STYLE_ARC_WIDTH, 0, coordPropValue },
  { "arc_rounded", LV_STYLE_ARC_ROUNDED, 0, boolPropValue },
  { "arc_image", LV_STYLE_BG_GRAD_STOP, 0, imagePropValue },
  { "text_color", LV_STYLE_TEXT_COLOR, LV_STYLE_TEXT_OPA, nullptr },
  { "font", LV_STYLE_TEXT_FONT, 0, fontPropValue },
  { "text_letter_space", LV_STYLE_TEXT_LETTER_SPACE, 0, coordPropValue },
  { "text_line_space", LV_STYLE_TEXT_LINE_SPACE, 0, coordPropValue },
  { "text_decor", LV_STYLE_TEXT_DECOR, 0, textDecorPropValue },
  { "text_align", LV_STYLE_TEXT_ALIGN, 0, textAlignPropValue },
  { "radius", LV_STYLE_RADIUS, 0, radiusPropValue },
  { "clip_corner", LV_STYLE_CLIP_CORNER, 0, boolPropValue },
  { "alpha", LV_STYLE_OPA, 0, intPropValue },
  { "alpha_layered", LV_STYLE_OPA_LAYERED, 0, intPropValue },
  // TODO: implement
  // - color_filter_dsc/color_filter_opa
  // - anim/anim_duration
  // - transition
  { "blend_mode", LV_STYLE_BLEND_MODE, 0, blendModePropValue },
  { "layout", LV_STYLE_LAYOUT, 0, layoutPropValue },
  { "flex_flow", LV_STYLE_FLEX_FLOW, 0, flexFlowPropValue },
  { "flex_main_place", LV_STYLE_FLEX_MAIN_PLACE, 0, flexAlignPropValue },
  { "flex_cross_place", LV_STYLE_FLEX_CROSS_PLACE, 0, flexAlignPropValue },
  { "flex_track_place", LV_STYLE_FLEX_TRACK_PLACE, 0, flexAlignPropValue },
  { "flex_grow", LV_STYLE_FLEX_GROW, 0, coordPropValue }, // TODO: unit unclear!!
  { "grid_columns", LV_STYLE_GRID_COLUMN_DSC_ARRAY, 0, gridTemplateArrayPropValue },
  { "grid_column_align", LV_STYLE_GRID_COLUMN_ALIGN, 0, gridAlignPropValue },
  { "grid_rows", LV_STYLE_GRID_ROW_DSC_ARRAY, 0, gridTemplateArrayPropValue },
  { "grid_row_align", LV_STYLE_GRID_ROW_ALIGN, 0, gridAlignPropValue },
  { "grid_x", LV_STYLE_GRID_CELL_COLUMN_POS, 0, coordPropValue },
  { "grid_x_align", LV_STYLE_GRID_CELL_X_ALIGN, 0, gridAlignPropValue },
  { "grid_dx", LV_STYLE_GRID_CELL_COLUMN_SPAN, 0, coordPropValue },
  { "grid_y", LV_STYLE_GRID_CELL_ROW_POS, 0, coordPropValue },
  { "grid_y_align", LV_STYLE_GRID_CELL_Y_ALIGN, 0, gridAlignPropValue },
  { "grid_dy", LV_STYLE_GRID_CELL_ROW_SPAN, 0, coordPropValue },
  // Terminator
  { nullptr, 0 }
};


static const PropDef* getPropDefFromName(const string aStylePropName)
{
  const PropDef* propDef = propDefs;
  while (propDef->propname) {
    if (uequals(aStylePropName, propDef->propname)) {
      return propDef;
    }
    propDef++;
  }
  return nullptr;
}



static lv_event_code_t getEventCodeFromName(const string aEventName)
{
  if (aEventName=="press") return LV_EVENT_PRESSED;
  if (aEventName=="pressing") return LV_EVENT_PRESSING;
  if (aEventName=="lost") return LV_EVENT_PRESS_LOST;
  if (aEventName=="shortclick") return LV_EVENT_SHORT_CLICKED;
  if (aEventName=="longpress") return LV_EVENT_LONG_PRESSED;
  if (aEventName=="longpress_repeat") return LV_EVENT_LONG_PRESSED_REPEAT;
  if (aEventName=="click") return LV_EVENT_CLICKED;
  if (aEventName=="release") return LV_EVENT_RELEASED;
  if (aEventName=="scroll_begin") return LV_EVENT_SCROLL_BEGIN;
  if (aEventName=="scroll_throw") return LV_EVENT_SCROLL_THROW_BEGIN;
  if (aEventName=="scroll_end") return LV_EVENT_SCROLL_END;
  if (aEventName=="scroll") return LV_EVENT_SCROLL;
  if (aEventName=="gesture") return LV_EVENT_GESTURE;
  if (aEventName=="key") return LV_EVENT_KEY;
  if (aEventName=="focus") return LV_EVENT_FOCUSED;
  if (aEventName=="defocus") return LV_EVENT_DEFOCUSED;
  if (aEventName=="leave") return LV_EVENT_LEAVE;
  if (aEventName=="hover") return LV_EVENT_HOVER_OVER;
  if (aEventName=="endhover") return LV_EVENT_HOVER_LEAVE;
  if (aEventName=="change") return LV_EVENT_VALUE_CHANGED;
  if (aEventName=="insert") return LV_EVENT_INSERT;
  if (aEventName=="refresh") return LV_EVENT_REFRESH;
  if (aEventName=="ready") return LV_EVENT_READY;
  if (aEventName=="cancel") return LV_EVENT_CANCEL;
  if (aEventName=="create") return LV_EVENT_CREATE;
  if (aEventName=="delete") return LV_EVENT_DELETE;
  if (aEventName=="load") return LV_EVENT_SCREEN_LOADED;
  if (aEventName=="unload") return LV_EVENT_SCREEN_UNLOADED;
  if (aEventName=="event") return LV_EVENT_ALL; // all
  return LV_EVENT_LAST; // invalid
}


static const char *eventName(lv_event_code_t aEvent)
{
  const char *etxt = "";
  switch(aEvent) {
    case LV_EVENT_PRESSED: etxt = "pressed"; break; // The object has been pressed
    case LV_EVENT_PRESSING: etxt = "pressing"; break; // The object is being pressed (sent continuously while pressing)
    case LV_EVENT_PRESS_LOST: etxt = "lost"; break; // Still pressing but slid from the objects
    case LV_EVENT_SHORT_CLICKED: etxt = "shortclick"; break; // Released before lLV_INDEV_LONG_PRESS_TIME. Not called if dragged.
    case LV_EVENT_LONG_PRESSED: etxt = "longpress"; break; // Pressing for LV_INDEV_LONG_PRESS_TIME time. Not called if dragged.
    case LV_EVENT_LONG_PRESSED_REPEAT: etxt = "longpress_repeat"; break; // Called after LV_INDEV_LONG_PRESS_TIME in every LV_INDEV_LONG_PRESS_REP_TIME ms. Not called if dragged.
    case LV_EVENT_CLICKED: etxt = "click"; break; // Called on release if not dragged (regardless to long press)
    case LV_EVENT_RELEASED: etxt = "released"; break; // Called in every case when the object has been released even if it was dragged
    case LV_EVENT_SCROLL_BEGIN: etxt = "scroll_begin"; break;
    case LV_EVENT_SCROLL_THROW_BEGIN: etxt = "scroll_throw"; break; // end of scroll with momentum
    case LV_EVENT_SCROLL_END: etxt = "scroll_end"; break;
    case LV_EVENT_SCROLL: etxt = "scroll"; break;
    case LV_EVENT_GESTURE: etxt = "gesture"; break;
    case LV_EVENT_KEY: etxt = "key"; break;
    case LV_EVENT_FOCUSED: etxt = "focused"; break;
    case LV_EVENT_DEFOCUSED: etxt = "defocused"; break;
    case LV_EVENT_LEAVE: etxt = "leave"; break; // defocused but still selected
    // LV_EVENT_HIT_TEST,            /**< Perform advanced hit-testing*/
    // LV_EVENT_INDEV_RESET,         /**< Indev has been reset*/
    case LV_EVENT_HOVER_OVER: etxt = "hover"; break; // Indev hover over object
    case LV_EVENT_HOVER_LEAVE: etxt = "endhover"; break; // Indev hover leave object
    // left out all drawing events
    case LV_EVENT_VALUE_CHANGED: etxt = "changed"; break; // The object's value has changed (i.e. slider moved)
    case LV_EVENT_INSERT: etxt = "insert"; break;
    case LV_EVENT_REFRESH: etxt = "refresh"; break;
    case LV_EVENT_READY: etxt = "ready"; break;
    case LV_EVENT_CANCEL: etxt = "cancel"; break; // "Close", "Cancel" or similar specific button has clicked
    // only some important ones for our context
    case LV_EVENT_CREATE: etxt = "create"; break; // Object is being created
    case LV_EVENT_DELETE: etxt = "delete"; break; // Object is being deleted
    case LV_EVENT_SCREEN_LOADED: etxt = "loaded"; break; // A screen was loaded
    case LV_EVENT_SCREEN_UNLOADED: etxt = "unloaded"; break; // A screen was unloaded
    default: etxt = "OTHER"; break;
  }
  return etxt;
}


// MARK: - LvGLUIObject

ErrorPtr LvGLUIObject::configure(JsonObjectPtr aConfig)
{
  ErrorPtr err;
  // iterate
  aConfig->resetKeyIteration();
  string key;
  JsonObjectPtr o;
  while (aConfig->nextKeyValue(key, o)) {
    if (key.substr(0,2)=="on") {
      // event handler
      lv_event_code_t eventcode = getEventCodeFromName(key.substr(2));
      if (eventcode!=LV_EVENT_LAST) {
        // valid on<event>
        err = setEventHandler(eventcode, o);
        if (Error::notOK(err)) break;
        // successfully handled
        continue;
      }
    }
    // not an event handler
    err = setProperty(key, o);
    if (Error::notOK(err)) break;
  }
  return err;
}


ErrorPtr LvGLUIObject::setProperty(const string& aName, JsonObjectPtr aValue)
{
  ErrorPtr err;
  if (aName=="name") {
    mName = aValue->stringValue();
  }
  else {
    err = TextError::err("unknown property '%s'", aName.c_str());
  }
  return err;
}


ErrorPtr LvGLUIObject::setEventHandler(lv_event_code_t aEventCode, JsonObjectPtr aHandler)
{
  ErrorPtr err;
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  err = TextError::err("object does not support handler for event '%s'", eventName(aEventCode));
  #else
  err = TextError::err("p44script events not supported");
  #endif
  return err;
}


// MARK: - LvGLUiTheme

ErrorPtr LvGLUiTheme::configure(JsonObjectPtr aConfig)
{
  ErrorPtr err;
  JsonObjectPtr o;
  const lv_font_t* font = lv_font_default();
  lv_color_t primary = lv_color_hex3(0x03A); // TODO: adjust for a nice default
  lv_color_t secondary = lv_color_hex3(0x015);
  bool isDark = false;
  if (aConfig->get("name", o)) {
    mName = o->stringValue();
    lv_style_value_t val;
    if (aConfig->get("primary", o)) {
      err = colorPropValue(o, val);
      if (Error::notOK(err)) return err;
      primary = val.color;
    }
    if (aConfig->get("secondary", o)) {
      err = colorPropValue(o, val);
      if (Error::notOK(err)) return err;
      secondary = val.color;
    }
    if (aConfig->get("dark", o)) {
      isDark = o->boolValue();
    }
    if (aConfig->get("font", o)) {
      err = fontPropValue(o, val);
      if (Error::notOK(err)) return err;
      font = (lv_font_t*)val.ptr;
    }
    // init now
    string baseName = "default";
    if (aConfig->get("base", o)) baseName = o->stringValue();
    // (re-)init theme
    if (false);
    #if LV_USE_THEME_SIMPLE
    else if (baseName=="simple") {
      mTheme = lv_theme_simple_init(mLvglui.display());
    }
    #endif
    #if LV_USE_THEME_MONO
    else if (baseName=="mono") {
      mTheme = lv_theme_mono_init(mLvglui.display(), isDark, font);
    }
    #endif
    #if LV_USE_THEME_DEFAULT
    else if (baseName=="default") {
      mTheme = lv_theme_default_init(
        mLvglui.display(),
        primary,
        secondary,
        isDark,
        font
      );
    }
    #endif
  }
  else {
    err = TextError::err("theme must have a name");
  }
  return err;
}


// MARK: - LvGLUiStyle

LvGLUiStyle::LvGLUiStyle(LvGLUi& aLvGLUI) :
  inherited(aLvGLUI),
  mGridColsP(nullptr),
  mGridRowsP(nullptr)
{
  lv_style_init(&mStyle);
}


LvGLUiStyle::~LvGLUiStyle()
{
  if (mGridColsP) { delete mGridColsP; mGridColsP = nullptr; }
  if (mGridRowsP) { delete mGridRowsP; mGridRowsP = nullptr; }
}


ErrorPtr LvGLUiStyle::setProperty(const string& aName, JsonObjectPtr aValue)
{
  // set style properties
  // - dummies not to be checked here
  if (aName=="selector") return ErrorPtr();
  // - set via property id
  ErrorPtr err;
  const PropDef* prop = getPropDefFromName(aName);
  if (prop) {
    if (prop->propconv) {
      lv_style_value_t propval;
      err = prop->propconv(aValue, propval);
      if (Error::isOK(err)) lv_style_set_prop(&mStyle, prop->propid, propval);
    }
    else if (prop->opa_propid) {
      // color and opacity combined
      lv_style_value_t color, opa;
      bool hasColor, hasOpa;
      err = colorPropValue(aValue, hasColor, color, hasOpa, opa);
      if (Error::isOK(err)) {
        if (hasColor) lv_style_set_prop(&mStyle, prop->propid, color);
        if (hasOpa) lv_style_set_prop(&mStyle, prop->opa_propid, opa);
      }
    }
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return err;
}


// MARK - Element Factory

static LVGLUiElementPtr createElement(LvGLUi& aLvGLUI, JsonObjectPtr aConfig, LvGLUiContainer* aParentP, bool aContainerByDefault)
{
  LVGLUiElementPtr elem;
  JsonObjectPtr o;
  string tn;
  if (aConfig->get("type", o)) {
    tn = o->stringValue();
  }
  // now create according to type
  if (tn=="panel") {
    elem = LVGLUiElementPtr(new LvGLUiPanel(aLvGLUI, aParentP));
  }
  else if (tn=="image") {
    elem = LVGLUiElementPtr(new LvGLUiImage(aLvGLUI, aParentP));
  }
  else if (tn=="label") {
    elem = LVGLUiElementPtr(new LvGLUiLabel(aLvGLUI, aParentP));
  }
  #if LV_USE_QRCODE
  else if (tn=="qrcode") {
    elem = LVGLUiElementPtr(new LvGLUiQRCode(aLvGLUI, aParentP));
  }
  #endif
  else if (tn=="button") {
    if (aConfig->get("image")) {
      elem = LVGLUiElementPtr(new LvGLUiImgButton(aLvGLUI, aParentP));
    }
    else {
      elem = LVGLUiElementPtr(new LvGLUiButton(aLvGLUI, aParentP));
    }
  }
  else if (tn=="image_button") {
    elem = LVGLUiElementPtr(new LvGLUiImgButton(aLvGLUI, aParentP));
  }
  #if LV_USE_SLIDER
  else if (tn=="slider") {
    elem = LVGLUiElementPtr(new LvGLUiSlider(aLvGLUI, aParentP));
  }
  #endif
  #if LV_USE_SWITCH
  else if (tn=="switch") {
    elem = LVGLUiElementPtr(new LvGLUiSwitch(aLvGLUI, aParentP));
  }
  #endif
  else {
    if (aContainerByDefault) {
      elem = LVGLUiElementPtr(new LvGLUiPanel(aLvGLUI, aParentP));
    }
    else {
      elem = LVGLUiElementPtr(new LvGLUiPlain(aLvGLUI, aParentP));
    }
  }
  return elem;
}


// MARK: - LVGLUiElement

const void* LVGLUiElement::imgSrc(const string& aSource)
{
  if (aSource.empty()) return nullptr;
  return aSource.c_str();
}


#if ENABLE_LVGLUI_SCRIPT_FUNCS

static void elementEventHandler(lv_event_t* aEvent)
{
  LVGLUIEventHandlerPtr handler = static_cast<LVGLUIEventHandler*>(lv_event_get_user_data(aEvent));
  if (handler) {
    handler->mLVGLUIElement.runEventScript(lv_event_get_code(aEvent), handler->mEventScript);
  }
}


LVGLUIEventHandler::LVGLUIEventHandler(LVGLUiElement& aElement, lv_event_code_t aEventCode, const string& aSource) :
  mLVGLUIElement(aElement),
  mEventScript(scriptbody+regular, eventName(aEventCode) /* static char* */, nullptr, nullptr)
{
  mEventScript.setSource(aSource);
  lv_obj_add_event_cb(mLVGLUIElement.mElement, elementEventHandler, aEventCode, this);
}


ErrorPtr LVGLUiElement::setEventHandler(lv_event_code_t aEventCode, JsonObjectPtr aHandler)
{
  ErrorPtr err;
  LVGLUIEventHandlerPtr handler = new LVGLUIEventHandler(*this, aEventCode, aHandler->stringValue());
  mEventHandlers.push_back(handler);
  if (aEventCode==LV_EVENT_REFRESH) mRefreshEventHandler = handler;
  return err;
}

#endif // ENABLE_LVGLUI_SCRIPT_FUNCS


LVGLUiElement::LVGLUiElement(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI),
  mParentP(aParentP),
  mElement(nullptr)
{
}


lv_obj_t* LVGLUiElement::lvParent()
{
  return mParentP ? mParentP->mElement : nullptr;
}


LVGLUiElement::~LVGLUiElement()
{
  clear();
}


void LVGLUiElement::clear()
{
  if (mElement) {
    if (!mParentP) {
      // root, owned by us, delete
      // Note: all child elements are also childs at the lvgl level and thus owned by the lvgl parent, not us
      lv_obj_delete(mElement);
    }
    mElement = nullptr;
  }
}



ErrorPtr LVGLUiElement::configure(JsonObjectPtr aConfig)
{
  ErrorPtr err;
  // iterate
  aConfig->resetKeyIteration();
  string key;
  JsonObjectPtr o;
  while (aConfig->nextKeyValue(key, o)) {
    err = setProperty(key, o);
    if (Error::notOK(err)) break;
  }
  return err;
}


ErrorPtr LVGLUiElement::setProperty(const string& aName, JsonObjectPtr aValue)
{
  if (!mElement) return TextError::err("trying to configure non-existing lv_obj");
  if (aName.substr(0,2)=="on") {
    // event handler
    lv_event_code_t eventcode = getEventCodeFromName(aName.substr(2));
    if (eventcode!=LV_EVENT_LAST) {
      // valid on<event>
      return setEventHandler(eventcode, aValue);
    }
  }
  // not an event handler, check other properties
  if (aName=="type") {
    // NOP: type is in config for element creation, has no significance here, but must not return error
  }
  // normal properties
  else if (aName=="x") {
    lv_obj_set_x(mElement, aValue->int32Value());
  }
  else if (aName=="y") {
    lv_obj_set_y(mElement, aValue->int32Value());
  }
  else if (aName=="dx") {
    lv_obj_set_width(mElement, aValue->int32Value());
  }
  else if (aName=="dy") {
    lv_obj_set_height(mElement, aValue->int32Value());
  }
  else if (aName=="align") {
    if (aValue->isType(json_type_object)) {
      // structured: { to:name, dx:nn, dy:nn }
      JsonObjectPtr o;
      LVGLUiElementPtr alignRef;
      lv_style_value_t alignmode;
      alignmode.num = LV_ALIGN_DEFAULT;
      lv_coord_t align_dx = 0;
      lv_coord_t align_dy = 0;
      if (aValue->get("to", o)) {
        alignRef = mLvglui.namedElement(o->stringValue(), mParentP); // sibling
      }
      if (aValue->get("dx", o)) {
        align_dx = o->int32Value();
      }
      if (aValue->get("dy", o)) {
        align_dy = o->int32Value();
      }
      if (aValue->get("mode", o)) {
        ErrorPtr err = alignPropValue(o, alignmode);
        if (Error::notOK(err)) return err;
      }
      if (alignRef) {
        // align to a existing object
        lv_obj_align_to(mElement, alignRef->mElement, (lv_align_t)alignmode.num, align_dx, align_dy);
      }
    }
    else {
      lv_style_value_t alignmode;
      ErrorPtr err = alignPropValue(aValue, alignmode);
      if (Error::notOK(err)) return err;
      lv_obj_set_align(mElement, (lv_align_t)alignmode.num);
    }
  }
  else if (aName=="style") {
    lv_style_t* style = nullptr;
    lv_style_selector_t selector;
    if (aValue->isType(json_type_array)) {
      if (aValue->arrayLength()==0) {
        // explicitly remove all styles
        lv_obj_remove_style_all(mElement);
      }
      else {
        // add one or multiple styles in an array
        for (int i=0; i<aValue->arrayLength(); i++) {
          ErrorPtr err = mLvglui.namedStyle(aValue->arrayGet(i), style, selector);
          if (Error::notOK(err)) return err;
          lv_obj_add_style(mElement, style, selector);
        }
      }
    }
    else {
      // add a single style or define local styling
      if (aValue->isType(json_type_object)) {
        // local: { "selector":"sta1|sta2", "styleprop1":val1 ... }
        // - we need the selector before we set any property!
        lv_style_selector_t selector = LV_STATE_DEFAULT;
        JsonObjectPtr o;
        if (aValue->get("selector", o)) {
          selector = getSelectorByList(o->stringValue());
          if (selector==LV_STATE_DEFAULT) return TextError::err("invalid local style selector '%s'", o->stringValue().c_str());
        }
        // - now iterate over local property overrides
        aValue->resetKeyIteration();
        string propName;
        while(aValue->nextKeyValue(propName, o)) {
          if (propName=="selector") continue; // ignore now, checked above
          const PropDef* prop = getPropDefFromName(propName);
          if (prop) {
            if (prop->propconv) {
              lv_style_value_t propval;
              ErrorPtr err = prop->propconv(o, propval);
              if (Error::notOK(err)) return err;
              lv_obj_set_local_style_prop(mElement, prop->propid, propval, selector);
            }
            else if (prop->opa_propid) {
              // color and opacity combined
              lv_style_value_t color, opa;
              bool hasColor, hasOpa;
              ErrorPtr err = colorPropValue(o, hasColor, color, hasOpa, opa);
              if (Error::notOK(err)) return err;
              if (hasColor) lv_obj_set_local_style_prop(mElement, prop->propid, color, selector);
              if (hasOpa) lv_obj_set_local_style_prop(mElement, prop->opa_propid, opa, selector);
            }
          }
          else {
            return TextError::err("unknown local style property '%s'", propName.c_str());
          }
        }
      }
      else {
        // add named style
        ErrorPtr err = mLvglui.namedStyle(aValue, style, selector);
        if (Error::notOK(err)) return err;
        lv_obj_add_style(mElement, style, selector);
      }
    } // single style
  }
  else if (aName=="flags") {
    uint32_t set=0, clr=0;
    getFlagChangesByList(aValue->stringValue(), set, clr);
    if (set!=0) lv_obj_add_flag(mElement, (lv_obj_flag_t)set);
    if (clr!=0) lv_obj_clear_flag(mElement, (lv_obj_flag_t)clr);
  }
  else if (aName=="state") {
    uint16_t set=0, clr=0;
    getStateChangesByList(aValue->stringValue(), set, clr);
    if (set!=0) lv_obj_add_state(mElement, (lv_state_t)set);
    if (clr!=0) lv_obj_clear_state(mElement, (lv_state_t)clr);
  }
  // generic content change
  else if (aName=="value") {
    setValue(aValue->int32Value(), 0); // w/o animation. Use script function setValue() for animated changes
  }
  else if (aName=="text") {
    setText(aValue->stringValue());
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


void LVGLUiElement::setText(const string &aNewText)
{
  string out;
  size_t pos = 0, sym;
  // replace &sym; pseudo-HTML-entities with symbols.
  while (true) {
    sym = aNewText.find('&',pos);
    if (sym==string::npos)
      break;
    out.append(aNewText.substr(pos, sym-pos));
    pos = sym+1;
    sym = aNewText.find(';',pos);
    if (sym==string::npos) {
      out += '&';
      continue;
    }
    string sy = getSymbolByName(aNewText.substr(pos, sym-pos));
    if (sy.empty()) {
      out += aNewText.substr(pos-1, sym-pos+2);
    }
    else {
      out += sy;
    }
    pos = sym+1;
  }
  // append rest
  out.append(aNewText.substr(pos));
  setTextRaw(out);
}



// MARK: - LVGLUiContainer

void LvGLUiContainer::clear()
{
  mNamedElements.clear();
  mAnonymousElements.clear();
  inherited::clear();
}


ErrorPtr LvGLUiContainer::addElements(JsonObjectPtr aElementConfigArray, LvGLUiContainer* aParent, bool aContainerByDefault)
{
  ErrorPtr err;
  for (int i = 0; i<aElementConfigArray->arrayLength(); ++i) {
    JsonObjectPtr elementConfig = aElementConfigArray->arrayGet(i);
    LVGLUiElementPtr uielement = createElement(mLvglui, elementConfig, aParent, aContainerByDefault);
    if (!uielement || !uielement->mElement) {
      err = TextError::err("unknown/invalid element type: %s", elementConfig->c_strValue());
      break;
    }
    err = uielement->configure(elementConfig);
    if (Error::notOK(err)) break;
    FOCUSLOG("Created Element '%s' from: %s", uielement->getName().c_str(), elementConfig->c_strValue());
    // add to named elements if it has a name
    if (!uielement->getName().empty()) {
      mNamedElements[uielement->getName()] = uielement;
    }
    else if (!aParent || uielement->wrapperNeeded()) {
      mAnonymousElements.push_back(uielement);
    }
    else {
      // this element does not need a wrapper, and has a parent which will release this child's memory
      // so we just need to make sure disposing of the wrapper will not delete the lv_obj
      uielement->mElement = nullptr; // cut lv_obj from the wrapper
    }
  }
  return err;
}


ErrorPtr LvGLUiContainer::configure(JsonObjectPtr aConfig)
{
  // configure basics (and skip "elements")
  ErrorPtr err = inherited::configure(aConfig);
  if (Error::isOK(err)) {
    // only now process the elements
    JsonObjectPtr o;
    if (aConfig->get("elements", o)) {
      err = addElements(o, this, false); // normal elements have a parent, and default elements are plain elements
    }
  }
  return err;
}


ErrorPtr LvGLUiContainer::setProperty(const string& aName, JsonObjectPtr aValue)
{
  if (aName=="elements") return ErrorPtr(); // ok, we'll process that later
  return inherited::setProperty(aName, aValue);
}



// MARK: - LvGLUiPlain - simple object with no child layout

LvGLUiPlain::LvGLUiPlain(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_obj_create(lvParent());
}


// MARK: - LvGLUiPanel - object that can have children

LvGLUiPanel::LvGLUiPanel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  // Note: any lv_obj can have children, only our wrapper differs
  mElement = lv_obj_create(lvParent());
}


// MARK: - LvGLUiImage

LvGLUiImage::LvGLUiImage(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_image_create(lvParent());
}


ErrorPtr LvGLUiImage::setProperty(const string& aName, JsonObjectPtr aValue)
{
  // configure params
  if (aName=="src") {
    if (setProp(mImgSrc, mLvglui.namedImageSource(aValue->stringValue()))) {
      lv_image_set_src(mElement, mImgSrc.c_str());
    }
  }
  else if (aName=="symbol") {
    lv_img_set_src(mElement, getSymbolByName(aValue->stringValue()));
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


void LvGLUiImage::setTextRaw(const string &aNewText)
{
  string imgTxt = LV_SYMBOL_DUMMY;
  imgTxt.append(aNewText);
  lv_img_set_src(mElement, imgTxt.c_str());
}



// MARK: - LvGLUiLabel

LvGLUiLabel::LvGLUiLabel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_label_create(lvParent());
}


ErrorPtr LvGLUiLabel::setProperty(const string& aName, JsonObjectPtr aValue)
{
  // configure params
  if (aName=="longmode") {
    string lm = aValue->stringValue();
    if (lm=="wrap") lv_label_set_long_mode(mElement, LV_LABEL_LONG_WRAP);
    else if (lm=="dot") lv_label_set_long_mode(mElement, LV_LABEL_LONG_DOT);
    else if (lm=="scroll") lv_label_set_long_mode(mElement, LV_LABEL_LONG_SCROLL);
    else if (lm=="circularscroll") lv_label_set_long_mode(mElement, LV_LABEL_LONG_SCROLL_CIRCULAR);
    else if (lm=="clip") lv_label_set_long_mode(mElement, LV_LABEL_LONG_CLIP);
  }
  else if (aName=="selectionstart") {
    lv_label_set_text_selection_start(mElement, aValue->int32Value());
  }
  else if (aName=="selectionend") {
    lv_label_set_text_selection_end(mElement, aValue->int32Value());
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


void LvGLUiLabel::setTextRaw(const string &aNewText)
{
  lv_label_set_text(mElement, aNewText.c_str());
}


#if LV_USE_QRCODE

// MARK: - LvGLUiQRCode

LvGLUiQRCode::LvGLUiQRCode(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_qrcode_create(lvParent());
}


ErrorPtr LvGLUiQRCode::setProperty(const string& aName, JsonObjectPtr aValue)
{
  // configure params
  if (aName=="size") {
    lv_qrcode_set_size(mElement, aValue->int32Value());
  }
  else if (aName=="darkcolor") {
    lv_style_value_t col;
    ErrorPtr err = colorPropValue(aValue, col);
    if (Error::notOK(err)) return err;
    lv_qrcode_set_dark_color(mElement, col.color);
  }
  else if (aName=="lightcolor") {
    lv_style_value_t col;
    ErrorPtr err = colorPropValue(aValue, col);
    if (Error::notOK(err)) return err;
    lv_qrcode_set_light_color(mElement, col.color);
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


void LvGLUiQRCode::setTextRaw(const string &aNewText)
{
  lv_qrcode_update(mElement, aNewText.c_str(), (uint32_t)aNewText.size());
}

#endif



// MARK: - LvGLUiButton

LvGLUiButton::LvGLUiButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP),
  mLabel(nullptr)
{
  mElement = lv_button_create(lvParent());
}


LvGLUiButton::~LvGLUiButton()
{
  if (mLabel) lv_obj_delete(mLabel);
}


ErrorPtr LvGLUiButton::setProperty(const string& aName, JsonObjectPtr aValue)
{
  // configure params
  if (aName=="label") {
    // convenience for text-labelled buttons
    if (mLabel) lv_obj_delete(mLabel);
    mLabel = lv_label_create(mElement);
    lv_obj_center(mLabel);
    setText(aValue->stringValue());
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


void LvGLUiButton::setTextRaw(const string &aNewText)
{
  if (mLabel) lv_label_set_text(mLabel, aNewText.c_str());
}


// MARK: - LvGLUiImgButton

const void* LvGLUiImgButton::imgBtnSrc(const string& aSource)
{
  const void* src = LVGLUiElement::imgSrc(aSource);
  if (src) {
    // avoid symbols in image buttons (these only work in normal images)
    if (lv_image_src_get_type(src)==LV_IMAGE_SRC_SYMBOL) {
      src = nullptr;
    }
  }
  return src;
}


LvGLUiImgButton::LvGLUiImgButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_imgbtn_create(lvParent());
}


ErrorPtr LvGLUiImgButton::setProperty(const string &aName, JsonObjectPtr aValue)
{
  // images
  // TODO: implement left/right images, for now we only have center
  if (aName=="released_image" || aName=="image") {
    if (setProp(relImgSrc, mLvglui.namedImageSource(aValue->stringValue())))
      lv_imagebutton_set_src(mElement, LV_IMAGEBUTTON_STATE_RELEASED, nullptr, relImgSrc.c_str(), nullptr);
  }
  else if (aName=="pressed_image") {
    if (setProp(prImgSrc, mLvglui.namedImageSource(aValue->stringValue())))
      lv_imagebutton_set_src(mElement, LV_IMAGEBUTTON_STATE_PRESSED, nullptr, prImgSrc.c_str(), nullptr);
  }
  else if (aName=="disabled_image") {
    if (setProp(inaImgSrc, mLvglui.namedImageSource(aValue->stringValue())))
      lv_imagebutton_set_src(mElement, LV_IMAGEBUTTON_STATE_DISABLED, nullptr, inaImgSrc.c_str(), nullptr);
  }
  else if (aName=="on_image") {
    if (setProp(tglPrImgSrc, mLvglui.namedImageSource(aValue->stringValue())))
      lv_imagebutton_set_src(mElement, LV_IMAGEBUTTON_STATE_CHECKED_PRESSED, nullptr, tglPrImgSrc.c_str(), nullptr);
  }
  else if (aName=="off_image") {
    if (setProp(tglRelImgSrc, mLvglui.namedImageSource(aValue->stringValue())))
      lv_imagebutton_set_src(mElement, LV_IMAGEBUTTON_STATE_CHECKED_RELEASED, nullptr, tglRelImgSrc.c_str(), nullptr);
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


// MARK: - LvGLUiBar

LvGLUiBar::LvGLUiBar(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_bar_create(lvParent());
}

ErrorPtr LvGLUiBar::setProperty(const string& aName, JsonObjectPtr aValue)
{
  if (aName=="min") {
    lv_bar_set_range(mElement, aValue->int32Value(), lv_bar_get_max_value(mElement));
  }
  else if (aName=="max") {
    lv_bar_set_range(mElement, lv_bar_get_min_value(mElement), aValue->int32Value());
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


void LvGLUiBar::setValue(int16_t aValue, uint16_t aAnimationTimeMs)
{
  if (aAnimationTimeMs>0) {
    LOG(LOG_ERR, "animation not yet supported");
    // TODO: implement
  }
  lv_bar_set_value(mElement, aValue, aAnimationTimeMs>0 ? LV_ANIM_ON : LV_ANIM_OFF);
}


#if LV_USE_SLIDER

// MARK: - LvGLUiSlider

LvGLUiSlider::LvGLUiSlider(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_slider_create(lvParent());
}


ErrorPtr LvGLUiSlider::setProperty(const string& aName, JsonObjectPtr aValue)
{
  if (aName=="min") {
    lv_slider_set_range(mElement, aValue->int32Value(), lv_slider_get_max_value(mElement));
  }
  else if (aName=="max") {
    lv_slider_set_range(mElement, lv_slider_get_min_value(mElement), aValue->int32Value());
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
}


int16_t LvGLUiSlider::getValue()
{
  return lv_slider_get_value(mElement);
}


void LvGLUiSlider::setValue(int16_t aValue, uint16_t aAnimationTimeMs)
{
  if (aAnimationTimeMs>0) {
    LOG(LOG_ERR, "animation not yet supported");
    // TODO: implement
  }
  lv_slider_set_value(mElement, aValue, aAnimationTimeMs>0 ? LV_ANIM_ON : LV_ANIM_OFF);
}

#endif


#if LV_USE_SWITCH

// MARK: - LvGLUiSwitch

LvGLUiSwitch::LvGLUiSwitch(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) :
  inherited(aLvGLUI, aParentP)
{
  mElement = lv_switch_create(lvParent());
}


int16_t LvGLUiSwitch::getValue()
{
  return lv_obj_has_state(mElement, LV_STATE_CHECKED) ? 1 : 0;
}


void LvGLUiSwitch::setValue(int16_t aValue, uint16_t aAnimationTimeMs)
{
  lv_obj_set_state(mElement, LV_STATE_CHECKED, aValue>0);
}

#endif



// MARK: - LvGLUi

static LvGLUi* gLvgluiP = nullptr;

LvGLUi::LvGLUi() :
  inherited(*this, nullptr),
  mDataPathResources(false),
  mEmptyScreen(nullptr)
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  ,mActivityTimeoutScript(scriptbody+regular, "activityTimeout")
  ,mActivationScript(scriptbody+regular, "activation")
  #endif
{
  mName = "LvGLUi";
  gLvgluiP = this;
}


LvGLUi::~LvGLUi()
{
  if (mEmptyScreen) lv_obj_delete(mEmptyScreen);
  mEmptyScreen = nullptr;
}


void LvGLUi::uiActivation(bool aActivated)
{
  if (aActivated) {
    runEventScript(LV_EVENT_REFRESH, mActivationScript);
  }
  else {
    runEventScript(LV_EVENT_REFRESH, mActivityTimeoutScript);
  }
}



void LvGLUi::clear()
{
  lv_screen_load(mEmptyScreen);
  // TODO: FIXME: how to clear the image cache?
  inherited::clear();
  mStyles.clear();
  mThemes.clear();
}


void LvGLUi::initForDisplay(lv_disp_t* aDisplay)
{
  mDisplay = aDisplay;
  mEmptyScreen = lv_obj_create(nullptr);
  clear();
}


ErrorPtr LvGLUi::setConfig(JsonObjectPtr aConfig)
{
  clear();
  return configure(aConfig);
}


lv_theme_t* LvGLUi::namedTheme(const string aThemeName)
{
  ThemeMap::iterator pos = mThemes.find(aThemeName);
  if (pos!=mThemes.end())
    return pos->second->mTheme;
  return nullptr;
}


lv_style_t* LvGLUi::namedStyle(const string aStyleName)
{
  // try custom styles first
  StyleMap::iterator pos = mStyles.find(aStyleName);
  if (pos!=mStyles.end()) return &pos->second->mStyle;
  // no built-in styles any more
  return nullptr;
}


ErrorPtr LvGLUi::namedStyle(JsonObjectPtr aStyleSpecOrDefinition, lv_style_t*& aStyleP, lv_style_selector_t& aSelector)
{
  ErrorPtr err;
  aSelector = LV_STATE_DEFAULT;
  string stylespec = aStyleSpecOrDefinition->stringValue();
  // style spec: <stylename>[:<sta1>[|sta2...]]
  size_t sep = stylespec.find_first_of(":");
  if (sep!=string::npos) {
    // state(s) specified
    aSelector = getSelectorByList(stylespec.substr(sep+1));
    if (aSelector==LV_STATE_DEFAULT) err = TextError::err("invalid selector '%s'", stylespec.substr(sep+1).c_str());
  }
  else {
    sep = stylespec.size();
  }
  if (Error::isOK(err)) {
    aStyleP = namedStyle(stylespec.substr(0,sep));
    if (!aStyleP) err = TextError::err("unknown style named '%s'", stylespec.substr(0,sep).c_str());
  }
  return err;
}


LVGLUiElementPtr LvGLUi::namedElement(string aElementPath, LVGLUiElementPtr aOrigin)
{
  if (!aOrigin || aElementPath.c_str()[0]!='.') {
    // absolute path lookup
    aOrigin = this;
  }
  else {
    aElementPath.erase(0,1); // remove dot
    if (aElementPath.size()==0) return aOrigin; // single dot means origin itself
  }
  // now process as relative lookup
  do {
    // find end of path element
    size_t sep = aElementPath.find(".");
    if (sep==0) {
      // (at least) double dot, step back to parent
      aOrigin = aOrigin->mParentP;
      aElementPath.erase(0,1);
      continue;
    }
    string elemname;
    if (sep==string::npos) {
      sep=aElementPath.size();
      elemname = aElementPath;
      aElementPath.clear();
    }
    else {
      elemname = aElementPath.substr(0,sep);
      aElementPath.erase(0,sep+1);
    }
    LVGLUiContainerPtr cont = boost::dynamic_pointer_cast<LvGLUiContainer>(aOrigin);
    if (cont) {
      ElementMap::iterator pos = cont->mNamedElements.find(elemname);
      if (pos!=cont->mNamedElements.end()) {
        aOrigin = pos->second;
        continue;
      }
    }
    return LVGLUiElementPtr(); // not found
  } while (aElementPath.size()>0);
  return aOrigin;
}


void LvGLUi::loadScreen(const string aScreenName)
{
  LVGLUiElementPtr screen = namedElement(aScreenName, &mLvglui);
  if (screen) {
    lv_screen_load(screen->mElement);
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    if (screen->mRefreshEventHandler) {
      screen->runEventScript(LV_EVENT_REFRESH, screen->mRefreshEventHandler->mEventScript);
    }
    #endif
  }
}


ErrorPtr LvGLUi::configure(JsonObjectPtr aConfig)
{
  // Note: do not call the inherited configure(), which would process properties in the json order
  //   We need to pick them in the right order here!
  JsonObjectPtr o;
  ErrorPtr err;
  // check for themes
  if (aConfig->get("themes", o)) {
    for (int i = 0; i<o->arrayLength(); ++i) {
      JsonObjectPtr themeConfig = o->arrayGet(i);
      LvGLUiThemePtr th = LvGLUiThemePtr(new LvGLUiTheme(*this));
      th->configure(themeConfig);
      if (th->getName().empty()) return TextError::err("theme must have a 'name'");
      mThemes[th->getName()] = th;
    }
  }
  // check for styles
  if (aConfig->get("styles", o)) {
    for (int i = 0; i<o->arrayLength(); ++i) {
      JsonObjectPtr styleConfig = o->arrayGet(i);
      LvGLUiStylePtr st = LvGLUiStylePtr(new LvGLUiStyle(*this));
      err = st->configure(styleConfig);
      if (Error::notOK(err)) return err;
      if (st->getName().empty()) return TextError::err("style must have a 'name'");
      mStyles[st->getName()] = st;
    }
  }
  // check for default theme
  if (aConfig->get("theme", o)) {
    lv_theme_t* th = namedTheme(o->stringValue());
    if (th) {
      lv_display_set_theme(mDisplay, th);
    }
  }
  // check for screens
  if (aConfig->get("screens", o)) {
    lv_display_set_default(mDisplay); // make sure screens are created on the correct display
    err = addElements(o, nullptr, true); // screens are just elements with no parent
    if (Error::notOK(err)) return err;
  }
  // check for start screen to load
  if (aConfig->get("startscreen", o)) {
    loadScreen(o->stringValue());
  }
  if (aConfig->get("resourceprefix", o)) {
    mResourcePrefix = o->stringValue();
  }
  if (aConfig->get("dataresources", o)) {
    mDataPathResources = o->boolValue();
  }
  // check for activation/deactivation scripts
  if (aConfig->get("activitytimeoutscript", o)) {
    mActivityTimeoutScript.setSource(o->stringValue());
  }
  if (aConfig->get("activationscript", o)) {
    mActivationScript.setSource(o->stringValue());
  }
  // simulate activity
  lv_display_trigger_activity(nullptr);
  return ErrorPtr();
}


string LvGLUi::imagePath(const string aImageSpec)
{
  string f;
  if (mDataPathResources) {
    f = Application::sharedApplication()->dataPath(aImageSpec, mResourcePrefix);
    if (access(f.c_str(), R_OK)>=0) return f;
  }
  f = Application::sharedApplication()->resourcePath(aImageSpec, mResourcePrefix);
  if (access(f.c_str(), R_OK)>=0) return f;
  return "";
}


void LvGLUi::setResourceLoadOptions(bool aFromDataPath, const string aPrefix)
{
  mDataPathResources = aFromDataPath;
  mResourcePrefix = aPrefix;
}


string LvGLUi::namedImageSource(const string& aImageSpec)
{
  if (aImageSpec.find('.')!=string::npos) {
    // consider this a file name
    string ip = imagePath(aImageSpec);
    return ip;
  }
  else {
    const char* sym = getSymbolByName(aImageSpec);
    if (sym) return sym; // symbol
    return LV_SYMBOL_DUMMY+aImageSpec; // text
  }
}


// MARK: - script support

#if ENABLE_LVGLUI_SCRIPT_FUNCS

using namespace P44Script;

ScriptObjPtr LvGLUi::representingScriptObj()
{
  if (!mRepresentingObj) {
    mRepresentingObj = new LVGLUiElementObj(this);
  }
  return mRepresentingObj;
}


void LVGLUiElement::runEventScript(lv_event_code_t aEventCode, ScriptHost& aScriptHost)
{
  const char* en = eventName(aEventCode);
  LOG(LOG_INFO, "--- Starting/queuing action script for event='%s', LVGLUiElement '%s'", en, getName().c_str());
  aScriptHost.setSharedMainContext(mLvglui.getScriptMainContext());
  // pass the event and sender as a thread-local variables
  SimpleVarContainer* eventThreadLocals = new SimpleVarContainer();
  eventThreadLocals->setMemberByName("event", new StringValue(en));
  eventThreadLocals->setMemberByName("sender", new LVGLUiElementObj(this));
  aScriptHost.run(regular|queue|concurrently|keepvars, boost::bind(&LvGLUi::scriptDone, this), eventThreadLocals, Infinite);
}


void LVGLUiElement::scriptDone()
{
  LOG(LOG_INFO, "--- Finished action script for LVGLUiElement '%s'", getName().c_str());
}


void LvGLUi::setScriptMainContext(ScriptMainContextPtr aScriptMainContext)
{
  mScriptMainContext = aScriptMainContext;
}


ScriptMainContextPtr LvGLUi::getScriptMainContext()
{
  if (!mScriptMainContext) {
    // default main context has the lvgl ui as instance object
    mScriptMainContext = StandardScriptingDomain::sharedDomain().newContext(new LVGLUiElementObj(this));
  }
  return mScriptMainContext;
}


// findobj(elementpath)
FUNC_ARG_DEFS(findobj, { text });
static void findobj_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  LVGLUiElementPtr elem = o->element()->getLvGLUi().namedElement(f->arg(0)->stringValue(), o->element());
  if (elem) {
    f->finish(new LVGLUiElementObj(elem));
    return;
  }
  f->finish(new AnnotatedNullValue("no such lvgl obj"));
}


// name()
static void name_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  f->finish(new StringValue(o->element()->getName()));
}


// parent()
static void parent_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  LVGLUiElement* parent = o->element()->mParentP;
  if (!parent) {
    f->finish(new AnnotatedNullValue("no parent obj"));
    return;
  }
  f->finish(new LVGLUiElementObj(parent));
}


// value()
static void value_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  f->finish(new NumericValue(o->element()->getValue()));
}


// setvalue(value [,animationtime])
FUNC_ARG_DEFS(setvalue, { numeric }, { numeric|optionalarg });
static void setvalue_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  int animtime = 0;
  if (f->arg(1)->defined()) {
    animtime = f->arg(1)->doubleValue()*1000; // make milliseconds
  }
  o->element()->setValue(f->arg(0)->intValue(), animtime);
  f->finish(o); // return myself for chaining calls
}


// settext(newtext)
FUNC_ARG_DEFS(settext, { text });
static void settext_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  o->element()->setText(f->arg(0)->stringValue());
  f->finish(o); // return myself for chaining calls
}


// refresh()
static void refresh_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  lv_obj_send_event(o->element()->mElement, LV_EVENT_REFRESH, nullptr);
  f->finish(o); // return myself for chaining calls
}


// showScreen(<screenname>)
FUNC_ARG_DEFS(showscreen, { text });
static void showscreen_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  o->element()->getLvGLUi().loadScreen(f->arg(0)->stringValue());
  f->finish();
}


// addstyle/removestyle
static void changestyle(BuiltinFunctionContextPtr f, bool aAdd)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  for(int i=0; i<f->numArgs(); i++) {
    lv_style_selector_t selector;
    lv_style_t* styleP;
    ErrorPtr err = o->element()->getLvGLUi().namedStyle(f->arg(i)->jsonValue(), styleP, selector);
    if (styleP) {
      if (aAdd) lv_obj_add_style(o->element()->mElement, styleP, selector);
      else lv_obj_remove_style(o->element()->mElement, styleP, selector);
    }
  }
  f->finish(o); // return myself for chaining calls
}
// addstyle(<style[:selector]> [,<style[:selector]>])
FUNC_ARG_DEFS(addstyle, { text|multiple });
static void addstyle_func(BuiltinFunctionContextPtr f)
{
  changestyle(f, true);
}
// removestyle(<style[:selector]> [,<style[:selector]>])
// removestyle() // reset to theme
// removestyle(null) // remove all styles
FUNC_ARG_DEFS(removestyle, { text|multiple });
static void removestyle_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  if(f->numArgs()==0) {
    // reset to theme, saving position and size
    lv_coord_t x = lv_obj_get_x(o->element()->mElement);
    lv_coord_t y = lv_obj_get_y(o->element()->mElement);
    lv_coord_t dx = lv_obj_get_width(o->element()->mElement);
    lv_coord_t dy = lv_obj_get_height(o->element()->mElement);
    lv_theme_apply(o->element()->mElement);
    lv_obj_set_x(o->element()->mElement, x);
    lv_obj_set_y(o->element()->mElement, y);
    lv_obj_set_width(o->element()->mElement, dx);
    lv_obj_set_height(o->element()->mElement, dy);
    f->finish(o); // return myself for chaining calls
    return;
  }
  else if (!f->arg(0)->defined()) {
    // remove all styles, including theme
    lv_obj_remove_style_all(o->element()->mElement);
    f->finish(o); // return myself for chaining calls
    return;
  }
  changestyle(f, false);
}


// set(propertyname, newvalue)   convenience function to set (configure) a single property
FUNC_ARG_DEFS(set, { text }, { anyvalid });
static void set_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  JsonObjectPtr cfgJSON = JsonObject::newObj();
  cfgJSON->add(f->arg(0)->stringValue().c_str(), f->arg(1)->jsonValue());
  o->element()->configure(cfgJSON);
  f->finish(o); // return myself for chaining calls
}


// configure(<filename|json|key=value>)
FUNC_ARG_DEFS(configure, { text|structured });
static void configure_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  JsonObjectPtr cfgJSON;
  ErrorPtr err;
  #if SCRIPTING_JSON_SUPPORT
  if (f->arg(0)->hasType(structured)) {
    // is already a JSON value, use it as-is
    cfgJSON = f->arg(0)->jsonValue();
  }
  else
  #endif
  {
    // JSON from string (or file if we have a JSON app)
    string cfgText = f->arg(0)->stringValue();
    // literal json or filename
    #if ENABLE_JSON_APPLICATION
    cfgJSON = Application::jsonObjOrResource(cfgText, &err);
    #else
    cfgJSON = JsonObject::objFromText(cfgText.c_str(), -1, &err);
    #endif
  }
  if (Error::isOK(err)) {
    err = o->element()->configure(cfgJSON);
  }
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err));
    return;
  }
  f->finish(o); // return view itself to allow chaining
}


static const BuiltinMemberDescriptor lvglobjFunctions[] = {
  FUNC_DEF_W_ARG(findobj, executable|structured),
  FUNC_DEF_NOARG(name, executable|text),
  FUNC_DEF_NOARG(parent, executable|structured),
  FUNC_DEF_NOARG(value, executable|numeric),
  FUNC_DEF_W_ARG(setvalue, executable|structured),
  FUNC_DEF_W_ARG(settext, executable|structured),
  FUNC_DEF_NOARG(refresh, executable|structured),
  FUNC_DEF_W_ARG(showscreen, executable|null),
  FUNC_DEF_W_ARG(set, executable|structured),
  FUNC_DEF_W_ARG(addstyle, executable|structured),
  FUNC_DEF_W_ARG(removestyle, executable|structured),
  FUNC_DEF_W_ARG(configure, executable|structured),
  { nullptr } // terminator
};

static BuiltInMemberLookup* sharedLvglobjFunctionLookupP = nullptr;

LVGLUiElementObj::LVGLUiElementObj(LVGLUiElementPtr aElement) :
  mElement(aElement)
{
  registerSharedLookup(sharedLvglobjFunctionLookupP, lvglobjFunctions);
}

#endif // ENABLE_LVGLUI_SCRIPT_FUNCS

#endif // ENABLE_LVGL
