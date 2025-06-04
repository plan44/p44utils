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

static const lv_font_t* getFontByName(const string aFontName)
{
  #if LV_FONT_MONTSERRAT_8
  if (aFontName=="montserrat8") return &lv_font_montserrat_8;
  #endif
  #if LV_FONT_MONTSERRAT_10
  if (aFontName=="montserrat10") return &lv_font_montserrat_10;
  #endif
  #if LV_FONT_MONTSERRAT_12
  if (aFontName=="montserrat12") return &lv_font_montserrat_12;
  #endif
  #if LV_FONT_MONTSERRAT_14
  if (aFontName=="montserrat14") return &lv_font_montserrat_14;
  #endif
  #if LV_FONT_MONTSERRAT_16
  if (aFontName=="montserrat16") return &lv_font_montserrat_16;
  #endif
  #if LV_FONT_MONTSERRAT_18
  if (aFontName=="montserrat18") return &lv_font_montserrat_18;
  #endif
  #if LV_FONT_MONTSERRAT_20
  if (aFontName=="montserrat20") return &lv_font_montserrat_20;
  #endif
  #if LV_FONT_MONTSERRAT_22
  if (aFontName=="montserrat22") return &lv_font_montserrat_22;
  #endif
  #if LV_FONT_MONTSERRAT_24
  if (aFontName=="montserrat24") return &lv_font_montserrat_24;
  #endif
  #if LV_FONT_MONTSERRAT_26
  if (aFontName=="montserrat26") return &lv_font_montserrat_26;
  #endif
  #if LV_FONT_MONTSERRAT_28
  if (aFontName=="montserrat28") return &lv_font_montserrat_28;
  #endif
  #if LV_FONT_MONTSERRAT_30
  if (aFontName=="montserrat30") return &lv_font_montserrat_30;
  #endif
  #if LV_FONT_MONTSERRAT_32
  if (aFontName=="montserrat32") return &lv_font_montserrat_32;
  #endif
  #if LV_FONT_MONTSERRAT_34
  if (aFontName=="montserrat34") return &lv_font_montserrat_34;
  #endif
  #if LV_FONT_MONTSERRAT_36
  if (aFontName=="montserrat36") return &lv_font_montserrat_36;
  #endif
  #if LV_FONT_MONTSERRAT_38
  if (aFontName=="montserrat38") return &lv_font_montserrat_38;
  #endif
  #if LV_FONT_MONTSERRAT_40
  if (aFontName=="montserrat40") return &lv_font_montserrat_40;
  #endif
  #if LV_FONT_MONTSERRAT_42
  if (aFontName=="montserrat42") return &lv_font_montserrat_42;
  #endif
  #if LV_FONT_MONTSERRAT_44
  if (aFontName=="montserrat44") return &lv_font_montserrat_44;
  #endif
  #if LV_FONT_MONTSERRAT_46
  if (aFontName=="montserrat46") return &lv_font_montserrat_46;
  #endif
  #if LV_FONT_MONTSERRAT_48
  if (aFontName=="montserrat48") return &lv_font_montserrat_48;
  #endif
  else return NULL;
}


static lv_layout_t getLayoutByName(const string aLayoutName)
{
  if (aLayoutName=="flex") return LV_LAYOUT_FLEX;
  else if (aLayoutName=="grid") return LV_LAYOUT_GRID;
  else return LV_LAYOUT_NONE;
}


lv_flex_align_t getFlexAlignByName(const string aFlexAlign)
{
  if (aFlexAlign=="start") return LV_FLEX_ALIGN_START;
  if (aFlexAlign=="end") return LV_FLEX_ALIGN_END;
  if (aFlexAlign=="evenly") return LV_FLEX_ALIGN_SPACE_EVENLY;
  if (aFlexAlign=="around") return LV_FLEX_ALIGN_SPACE_AROUND;
  if (aFlexAlign=="between") return LV_FLEX_ALIGN_SPACE_BETWEEN;
  return LV_FLEX_ALIGN_CENTER;
}


lv_flex_flow_t getFlexFlowByName(const string aFlexFlow)
{
  const char* p = aFlexFlow.c_str();
  string part;
  uint8_t flow = LV_FLEX_FLOW_ROW;
  while (nextPart(p, part, ',')) {
    if (part=="column") flow |= LV_FLEX_COLUMN;
    else if (part=="wrap") flow |= LV_FLEX_WRAP;
    else if (part=="reverse") flow |= LV_FLEX_REVERSE;
  }
  return (lv_flex_flow_t)flow;
}


lv_grid_align_t getGridAlignByName(const string aFlexAlign)
{
  if (aFlexAlign=="start") return LV_GRID_ALIGN_START;
  if (aFlexAlign=="end") return LV_GRID_ALIGN_END;
  if (aFlexAlign=="evenly") return LV_GRID_ALIGN_SPACE_EVENLY;
  if (aFlexAlign=="around") return LV_GRID_ALIGN_SPACE_AROUND;
  if (aFlexAlign=="between") return LV_GRID_ALIGN_SPACE_BETWEEN;
  return LV_GRID_ALIGN_CENTER;
}


bool getGridTemplateArray(int32_t*& aGridTemplateP, JsonObjectPtr aTemplates)
{
  if (aGridTemplateP) { delete aGridTemplateP; aGridTemplateP = nullptr; }
  int n = aTemplates->arrayLength();
  if (n>0) {
    aGridTemplateP = new int32_t[n+1]; // plus terminator
    for (int i = 0; i<n; i++) {
      aGridTemplateP[i] = aTemplates->arrayGet(i)->int32Value();
    }
    aGridTemplateP[n] = LV_GRID_TEMPLATE_LAST;
    return true;
  }
  return false;
}


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



static lv_color_t getColorFromString(const string aColorSpec, lv_opa_t* aOpacityP = nullptr)
{
  int r = 0, g = 0, b = 0;
  lv_opa_t a;
  bool hasOpacity = false;
  size_t n = aColorSpec.size();
  if (n>0) {
    if (aColorSpec[0]!='#') {
      // check palette
      // syntax <palettecolorname>[+<lighten_level>|-<darken_level>]
      // - find adjustment
      size_t i = aColorSpec.find_first_of("+-");
      int32_t adj = 0;
      if (i!=string::npos) {
        sscanf(aColorSpec.c_str()+i, "%d", &adj);
      }
      else {
        i = aColorSpec.size();
      }
      // - find color
      string colname = aColorSpec.substr(0, i);
      if (colname=="black") return lv_color_black();
      if (colname=="white") return lv_color_white();
      if (colname=="transparent") {
        if (aOpacityP) *aOpacityP = 0;
        return lv_color_black();
      }
      lv_palette_t pi = getPaletteEntryFromColorSpec(colname);
      if (pi!=LV_PALETTE_NONE) {
        if (adj==0) return lv_palette_main(pi);
        else if (adj>0) return lv_palette_lighten(pi, adj);
        else return lv_palette_darken(pi, -adj);
      }
    }
    else {
      // web color
      // syntax: #rrggbb or #rgb or #aarrggbb or #argb
      n--; // string size without # !
      uint32_t h;
      if (sscanf(aColorSpec.c_str()+1, "%x", &h)==1) {
        if (n<=4) {
          // short form RGB or ARGB
          if (n==4) { hasOpacity = true; a = (h>>12)&0xF; a |= a<<4; }
          r = (h>>8)&0xF; r |= r<<4;
          g = (h>>4)&0xF; g |= g<<4;
          b = (h>>0)&0xF; b |= b<<4;
        }
        else {
          // long form RRGGBB or AARRGGBB
          a = 255;
          if (n==8) { hasOpacity = true; a = (h>>24)&0xFF; }
          r = (h>>16)&0xFF;
          g = (h>>8)&0xFF;
          b = (h>>0)&0xFF;
        }
      }
    }
  }
  if (aOpacityP && hasOpacity) *aOpacityP = a;
  return lv_color_make(r,g,b);
}


static lv_border_side_t getBorderSidesFromList(const string aBorderSides)
{
  const char* p = aBorderSides.c_str();
  string part;
  uint8_t sides = LV_BORDER_SIDE_NONE;
  while (nextPart(p, part, ',')) {
    if (part=="bottom") sides |= LV_BORDER_SIDE_BOTTOM;
    else if (part=="top") sides |= LV_BORDER_SIDE_TOP;
    else if (part=="left") sides |= LV_BORDER_SIDE_LEFT;
    else if (part=="right") sides |= LV_BORDER_SIDE_RIGHT;
    else if (part=="all") sides |= LV_BORDER_SIDE_FULL;
    else if (part=="internal") sides |= LV_BORDER_SIDE_INTERNAL;
  }
  return (lv_border_side_t)sides;
}


static lv_align_t getAlignModeByName(const string aAlignMode)
{
  const char *p = aAlignMode.c_str();
  bool in = true; // default to in
  bool top = false;
  bool mid = false;
  bool bottom = false;
  bool left = false;
  bool right = false;
  string tok;
  if (aAlignMode=="center") return LV_ALIGN_CENTER; // special case not combinable from the elements below
  while (nextPart(p, tok, ',')) {
    if (tok=="top") top = true;
    else if (tok=="mid") mid = true;
    else if (tok=="bottom") bottom = true;
    else if (tok=="left") left = true;
    else if (tok=="right") right = true;
    else if (tok=="in") in = true;
    else if (tok=="out") in = false;
  }
  if (in && top && left) return LV_ALIGN_TOP_LEFT;
  if (in && top && mid) return LV_ALIGN_TOP_MID;
  if (in && top && right) return LV_ALIGN_TOP_RIGHT;
  if (in && bottom && left) return LV_ALIGN_BOTTOM_LEFT;
  if (in && bottom && mid) return LV_ALIGN_BOTTOM_MID;
  if (in && bottom && right) return LV_ALIGN_BOTTOM_RIGHT;
  if (in && left && mid) return LV_ALIGN_LEFT_MID;
  if (in && right && mid) return LV_ALIGN_RIGHT_MID;
  if (!in && top && left) return LV_ALIGN_OUT_TOP_LEFT;
  if (!in && top && mid) return LV_ALIGN_OUT_TOP_MID;
  if (!in && top && right) return LV_ALIGN_OUT_TOP_RIGHT;
  if (!in && bottom && left) return LV_ALIGN_OUT_BOTTOM_LEFT;
  if (!in && bottom && mid) return LV_ALIGN_OUT_BOTTOM_MID;
  if (!in && bottom && right) return LV_ALIGN_OUT_BOTTOM_RIGHT;
  if (!in && left && top) return LV_ALIGN_OUT_LEFT_TOP;
  if (!in && left && mid) return LV_ALIGN_OUT_LEFT_MID;
  if (!in && left && bottom) return LV_ALIGN_OUT_LEFT_BOTTOM;
  if (!in && right && top) return LV_ALIGN_OUT_RIGHT_TOP;
  if (!in && right && mid) return LV_ALIGN_OUT_RIGHT_MID;
  if (!in && right && bottom) return LV_ALIGN_OUT_RIGHT_BOTTOM;
  return LV_ALIGN_DEFAULT;
}


static lv_grad_dir_t getGradientDirByName(const string aGradientDir)
{
  if (aGradientDir=="vertical") return LV_GRAD_DIR_VER;
  if (aGradientDir=="horizontal") return LV_GRAD_DIR_HOR;
  if (aGradientDir=="linear") return LV_GRAD_DIR_LINEAR;
  if (aGradientDir=="radial") return LV_GRAD_DIR_RADIAL;
  if (aGradientDir=="conical") return LV_GRAD_DIR_CONICAL;
  return LV_GRAD_DIR_NONE;
}


static lv_text_decor_t getTextDecorByName(const string aTextDecor)
{
  const char* p = aTextDecor.c_str();
  string part;
  uint8_t decor = LV_TEXT_DECOR_NONE;
  while (nextPart(p, part, ',')) {
    if (part=="underline") decor |= LV_TEXT_DECOR_UNDERLINE;
    else if (part=="strikethrough") decor |= LV_TEXT_DECOR_STRIKETHROUGH;
  }
  return (lv_text_decor_t)decor;
}


static lv_text_align_t getTextAlignByName(const string aTextAlign)
{
  if (aTextAlign=="left") return LV_TEXT_ALIGN_LEFT;
  if (aTextAlign=="center") return LV_TEXT_ALIGN_CENTER;
  if (aTextAlign=="right") return LV_TEXT_ALIGN_RIGHT;
  return LV_TEXT_ALIGN_AUTO;
}


static lv_blend_mode_t getBlendModeByName(const string aBlendMode)
{
  if (aBlendMode=="additive") return LV_BLEND_MODE_ADDITIVE;
  if (aBlendMode=="subtractive") return LV_BLEND_MODE_SUBTRACTIVE;
  if (aBlendMode=="multiply") return LV_BLEND_MODE_MULTIPLY;
  return LV_BLEND_MODE_NORMAL;
}


static lv_coord_t getCoordFromJson(JsonObjectPtr aCoord)
{
  // strings might be values in other units than pixels
  lv_coord_t coord = 0;
  if (aCoord->isType(json_type_string)) {
    string s = aCoord->stringValue();
    int32_t v;
    char unit = 0;
    if (sscanf(s.c_str(), "%d%c", &v, &unit)>=1) {
      if (unit=='%') coord = lv_pct(v);
      else coord = v; // no other recognized units so far
    }
  }
  else {
    coord = aCoord->int32Value();
  }
  return coord;
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
  const lv_font_t* font = NULL;
  lv_color_t primary = lv_color_hex3(0x03A); // TODO: adjust for a nice default
  lv_color_t secondary = lv_color_hex3(0x015);
  bool isDark = false;
  if (aConfig->get("name", o)) {
    mName = o->stringValue();
    if (aConfig->get("primary", o)) {
      primary = getColorFromString(o->stringValue());
    }
    if (aConfig->get("secondary", o)) {
      secondary = getColorFromString(o->stringValue());
    }
    if (aConfig->get("dark", o)) {
      isDark = o->boolValue();
    }
    if (aConfig->get("font", o)) {
      font = getFontByName(o->stringValue());
      if (!font) return TextError::err("unknown font '%s'", o->stringValue().c_str());
    }
    // (re-)init theme
    mTheme = lv_theme_default_init(
      mLvglui.display(),
      primary,
      secondary,
      isDark,
      font
    );
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
  // - size and position
  // common properties
  if (aName=="x") {
    lv_style_set_x(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="y") {
    lv_style_set_y(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="dx") {
    lv_style_set_width(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="min_dx") {
    lv_style_set_min_width(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="max_dx") {
    lv_style_set_max_width(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="dy") {
    lv_style_set_height(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="min_dy") {
    lv_style_set_min_height(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="max_dy") {
    lv_style_set_max_height(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="length") {
    lv_style_set_length(&mStyle, aValue->int32Value());
  }
  else if (aName=="align") {
    lv_style_set_align(&mStyle, getAlignModeByName(aValue->stringValue()));
  }
  else if (aName=="transform_dx") {
    lv_style_set_transform_width(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="transform_dy") {
    lv_style_set_transform_height(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="translate_dx") {
    lv_style_set_translate_x(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="translate_dy") {
    lv_style_set_translate_y(&mStyle, getCoordFromJson(aValue));
  }
  else if (aName=="scale_x") {
    lv_style_set_transform_scale_x(&mStyle, aValue->doubleValue()*256);
  }
  else if (aName=="scale_y") {
    lv_style_set_transform_scale_y(&mStyle, aValue->doubleValue()*256);
  }
  else if (aName=="rotation") {
    lv_style_set_transform_rotation(&mStyle, aValue->doubleValue()*10);
  }
  else if (aName=="pivot_x") {
    lv_style_set_transform_pivot_x(&mStyle, aValue->int32Value());
  }
  else if (aName=="pivot_y") {
    lv_style_set_transform_pivot_y(&mStyle, aValue->int32Value());
  }
  else if (aName=="skew_x") {
    lv_style_set_transform_skew_x(&mStyle, aValue->doubleValue()*10);
  }
  else if (aName=="skew_y") {
    lv_style_set_transform_skew_y(&mStyle, aValue->doubleValue()*10);
  }
  // - padding
  else if (aName=="padding") {
    lv_style_set_pad_all(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="padding_top") {
    lv_style_set_pad_top(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="padding_bottom") {
    lv_style_set_pad_bottom(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="padding_left") {
    lv_style_set_pad_left(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="padding_right") {
    lv_style_set_pad_right(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="padding_row") {
    lv_style_set_pad_row(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="padding_column") {
    lv_style_set_pad_column(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  // - margin
  else if (aName=="margin_top") {
    lv_style_set_margin_top(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="margin_bottom") {
    lv_style_set_margin_bottom(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="margin_left") {
    lv_style_set_margin_left(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="margin_right") {
    lv_style_set_margin_right(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  // - background
  else if (aName=="color") {
    lv_opa_t opacity = 255;
    lv_style_set_bg_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_bg_main_opa(&mStyle, opacity);
  }
  else if (aName=="gradient_color") {
    lv_opa_t opacity = 255;
    lv_style_set_bg_grad_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_bg_grad_opa(&mStyle, opacity);
  }
  else if (aName=="gradient_dir") {
    lv_style_set_bg_grad_dir(&mStyle, getGradientDirByName(aValue->stringValue()));
  }
  else if (aName=="gradient_start") {
    // Note: we call that start, not main stop
    lv_style_set_bg_main_stop(&mStyle, aValue->doubleValue()*2.55);
  }
  else if (aName=="gradient_stop") {
    lv_style_set_bg_grad_stop(&mStyle, aValue->doubleValue()*2.55);
  }
  else if (aName=="bg_image") {
    // FIXME: similar to what we did in LvglUIImage
    #warning %%% to implement
    return TextError::err("'%s' not yet implemented", aName.c_str());
  }
  else if (aName=="bg_recoloring") {
    lv_opa_t opacity = 255;
    lv_style_set_bg_image_recolor(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_bg_image_recolor_opa(&mStyle, opacity);
  }
  else if (aName=="bg_tiled") {
    lv_style_set_bg_image_tiled(&mStyle, aValue->boolValue());
  }
  // - border
  else if (aName=="border_color") {
    lv_opa_t opacity = 255;
    lv_style_set_border_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_border_opa(&mStyle, opacity);
  }
  else if (aName=="border_width") {
    lv_style_set_border_width(&mStyle, aValue->int32Value());
  }
  else if (aName=="border_sides") {
    lv_style_set_border_side(&mStyle, getBorderSidesFromList(aValue->stringValue()));
  }
  else if (aName=="border_post") {
    lv_style_set_border_post(&mStyle, aValue->boolValue());
  }
  // - outline
  else if (aName=="outline_color") {
    lv_opa_t opacity = 255;
    lv_style_set_outline_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_outline_opa(&mStyle, opacity);
  }
  else if (aName=="outline_width") {
    lv_style_set_outline_width(&mStyle, aValue->int32Value());
  }
  else if (aName=="outline_pad") {
    lv_style_set_outline_pad(&mStyle, aValue->int32Value());
  }
  // - shadow
  else if (aName=="shadow_color") {
    lv_opa_t opacity = 255;
    lv_style_set_shadow_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_shadow_opa(&mStyle, opacity);
  }
  else if (aName=="shadow_width") {
    lv_style_set_shadow_width(&mStyle, aValue->int32Value());
  }
  else if (aName=="shadow_dx") {
    lv_style_set_shadow_offset_x(&mStyle, aValue->int32Value());
  }
  else if (aName=="shadow_dy") {
    lv_style_set_shadow_offset_y(&mStyle, aValue->int32Value());
  }
  else if (aName=="shadow_spread") {
    lv_style_set_shadow_spread(&mStyle, aValue->int32Value());
  }
  // - image
  else if (aName=="image_alpha") {
    lv_style_set_image_opa(&mStyle, (lv_opa_t)aValue->int32Value());
  }
  else if (aName=="image_recoloring") {
    lv_opa_t opacity = 255;
    lv_style_set_image_recolor(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_image_recolor_opa(&mStyle, opacity);
  }
  // - line
  else if (aName=="line_color") {
    lv_opa_t opacity = 255;
    lv_style_set_line_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_line_opa(&mStyle, opacity);
  }
  else if (aName=="line_width") {
    lv_style_set_line_width(&mStyle, aValue->int32Value());
  }
  else if (aName=="line_dash") {
    lv_style_set_line_dash_width(&mStyle, aValue->int32Value());
  }
  else if (aName=="line_gap") {
    lv_style_set_line_dash_gap(&mStyle, aValue->int32Value());
  }
  else if (aName=="line_rounded") {
    lv_style_set_line_rounded(&mStyle, aValue->boolValue());
  }
  // - arc
  else if (aName=="arc_color") {
    lv_opa_t opacity = 255;
    lv_style_set_arc_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_arc_opa(&mStyle, opacity);
  }
  else if (aName=="arc_width") {
    lv_style_set_arc_width(&mStyle, aValue->int32Value());
  }
  else if (aName=="arc_rounded") {
    lv_style_set_arc_rounded(&mStyle, aValue->boolValue());
  }
  else if (aName=="arc_image") {
    // FIXME: similar to what we did in LvglUIImage
    #warning %%% to implement
    return TextError::err("'%s' not yet implemented", aName.c_str());
  }
  // - text
  else if (aName=="text_color") {
    lv_opa_t opacity = 255;
    lv_style_set_text_color(&mStyle, getColorFromString(aValue->stringValue(), &opacity));
    lv_style_set_text_opa(&mStyle, opacity);
  }
  else if (aName=="font") {
    const lv_font_t* font = getFontByName(aValue->stringValue());
    if (!font) return TextError::err("font '%s' not found", aValue->stringValue().c_str());
    lv_style_set_text_font(&mStyle, font);
  }
  else if (aName=="text_letter_space") {
    lv_style_set_text_letter_space(&mStyle, aValue->int32Value());
  }
  else if (aName=="text_line_space") {
    lv_style_set_text_line_space(&mStyle, aValue->int32Value());
  }
  else if (aName=="text_decor") {
    lv_style_set_text_decor(&mStyle, getTextDecorByName(aValue->stringValue()));
  }
  else if (aName=="text_align") {
    lv_style_set_text_align(&mStyle, getTextAlignByName(aValue->stringValue()));
  }
  // - miscellaneous
  else if (aName=="radius") {
    if (aValue->stringValue()=="circle") lv_style_set_radius(&mStyle, LV_RADIUS_CIRCLE);
    else lv_style_set_radius(&mStyle, (lv_coord_t)aValue->int32Value());
  }
  else if (aName=="clip_corner") {
    lv_style_set_clip_corner(&mStyle, aValue->boolValue());
  }
  else if (aName=="alpha") {
    lv_style_set_opa(&mStyle, (lv_opa_t)aValue->int32Value());
  }
  else if (aName=="alpha_layered") {
    lv_style_set_opa_layered(&mStyle, (lv_opa_t)aValue->int32Value());
  }
  // TODO: implement
  // - color_filter_dsc/color_filter_opa
  // - anim/anim_duration
  // - transition
  else if (aName=="blend_mode") {
    lv_style_set_blend_mode(&mStyle, getBlendModeByName(aValue->stringValue()));
  }
  else if (aName=="layout") {
    lv_style_set_layout(&mStyle, getLayoutByName(aValue->stringValue()));
  }
  // - flex
  else if (aName=="flex_flow") {
    lv_style_set_flex_flow(&mStyle, getFlexFlowByName(aValue->stringValue()));
  }
  else if (aName=="flex_main_place") {
    lv_style_set_flex_main_place(&mStyle, getFlexAlignByName(aValue->stringValue()));
  }
  else if (aName=="flex_cross_place") {
    lv_style_set_flex_cross_place(&mStyle, getFlexAlignByName(aValue->stringValue()));
  }
  else if (aName=="flex_track_place") {
    lv_style_set_flex_track_place(&mStyle, getFlexAlignByName(aValue->stringValue()));
  }
  else if (aName=="flex_grow") {
    lv_style_set_flex_grow(&mStyle, getFlexAlignByName(aValue->stringValue()));
  }
  // - grid
  else if (aName=="grid_columns") {
    int32_t* columns;
    if (getGridTemplateArray(columns, aValue)) {
      lv_style_set_grid_column_dsc_array(&mStyle, columns);
    }
  }
  else if (aName=="grid_column_align") {
    lv_style_set_grid_column_align(&mStyle, getGridAlignByName(aValue->stringValue()));
  }
  else if (aName=="grid_rows") {
    int32_t* rows;
    if (getGridTemplateArray(rows, aValue)) {
      lv_style_set_grid_row_dsc_array(&mStyle, rows);
    }
  }
  else if (aName=="grid_row_align") {
    lv_style_set_grid_row_align(&mStyle, getGridAlignByName(aValue->stringValue()));
  }
  else if (aName=="grid_x") {
    lv_style_set_grid_cell_column_pos(&mStyle, aValue->int32Value());
  }
  else if (aName=="grid_x_align") {
    lv_style_set_grid_cell_x_align(&mStyle, getGridAlignByName(aValue->stringValue()));
  }
  else if (aName=="grid_dx") {
    lv_style_set_grid_cell_column_span(&mStyle, aValue->int32Value());
  }
  else if (aName=="grid_y") {
    lv_style_set_grid_cell_row_pos(&mStyle, aValue->int32Value());
  }
  else if (aName=="grid_y_align") {
    lv_style_set_grid_cell_y_align(&mStyle, getGridAlignByName(aValue->stringValue()));
  }
  else if (aName=="grid_dy") {
    lv_style_set_grid_cell_row_span(&mStyle, aValue->int32Value());
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  return ErrorPtr();
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
    if (aConfig->get("image"))
      elem = LVGLUiElementPtr(new LvGLUiImgButton(aLvGLUI, aParentP));
    else
      elem = LVGLUiElementPtr(new LvGLUiButton(aLvGLUI, aParentP));
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
    if (aContainerByDefault)
      elem = LVGLUiElementPtr(new LvGLUiPanel(aLvGLUI, aParentP));
    else
      elem = LVGLUiElementPtr(new LvGLUiPlain(aLvGLUI, aParentP));
  }
  return elem;
}


// MARK: - LVGLUiElement

const void* LVGLUiElement::imgSrc(const string& aSource)
{
  if (aSource.empty()) return NULL;
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
  mElement(NULL)
{
}


lv_obj_t* LVGLUiElement::lvParent()
{
  return mParentP ? mParentP->mElement : NULL;
}


LVGLUiElement::~LVGLUiElement()
{
  clear();
}


void LVGLUiElement::clear()
{
  if (mElement) {
    lv_obj_del(mElement); // delete element and all of its children on the lvgl level
    mElement = NULL;
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
      lv_align_t alignmode = LV_ALIGN_DEFAULT;
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
        alignmode = getAlignModeByName(o->stringValue());
      }
      if (alignRef) {
        // align to a existing object
        lv_obj_align_to(mElement, alignRef->mElement, alignmode, align_dx, align_dy);
      }
    }
    else {
      lv_obj_set_align(mElement, getAlignModeByName(aValue->stringValue()));
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
          ErrorPtr err = mLvglui.namedOrAdHocStyle(aValue->arrayGet(i), style, selector);
          if (Error::notOK(err)) return err;
          lv_obj_add_style(mElement, style, selector);
        }
      }
    }
    else {
      // add a single style
      ErrorPtr err = mLvglui.namedOrAdHocStyle(aValue, style, selector);
      if (Error::notOK(err)) return err;
      lv_obj_add_style(mElement, style, selector);
    }
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
      uielement->mElement = NULL; // cut lv_obj from the wrapper
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
  mElement = lv_img_create(lvParent());
}


ErrorPtr LvGLUiImage::setProperty(const string& aName, JsonObjectPtr aValue)
{
  // configure params
  if (aName=="src") {
    if (setProp(mImgSrc, mLvglui.namedImageSource(aValue->stringValue()))) {
      lv_img_set_src(mElement, mImgSrc.c_str());
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
    lv_qrcode_set_dark_color(mElement, getColorFromString(aValue->stringValue()));
  }
  else if (aName=="lightcolor") {
    lv_qrcode_set_light_color(mElement, getColorFromString(aValue->stringValue()));
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
      src = NULL;
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

static LvGLUi* gLvgluiP = NULL;

LvGLUi::LvGLUi() :
  inherited(*this, NULL),
  mDataPathResources(false)
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  ,mActivityTimeoutScript(scriptbody+regular, "activityTimeout")
  ,mActivationScript(scriptbody+regular, "activation")
  #endif
{
  mName = "LvGLUi";
  gLvgluiP = this;
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
  // TODO: FIXME: how to clear the image cache?
  inherited::clear();
  mStyles.clear();
  mAdhocStyles.clear();
  mThemes.clear();
}


void LvGLUi::initForDisplay(lv_disp_t* aDisplay)
{
  clear();
  mDisplay = aDisplay;
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
  return NULL;
}


lv_style_t* LvGLUi::namedStyle(const string aStyleName)
{
  // try custom styles first
  StyleMap::iterator pos = mStyles.find(aStyleName);
  if (pos!=mStyles.end()) return &pos->second->mStyle;
  // no built-in styles any more
  return nullptr;
}


ErrorPtr LvGLUi::namedOrAdHocStyle(JsonObjectPtr aStyleSpecOrDefinition, lv_style_t*& aStyleP, lv_style_selector_t& aSelector)
{
  ErrorPtr err;
  aSelector = LV_STATE_DEFAULT;
  if (aStyleSpecOrDefinition->isType(json_type_string)) {
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
  }
  else if (aStyleSpecOrDefinition->isType(json_type_object)) {
    // ad-hoc style: { "states":"sta1|sta2", "styleprop1":val1 ... }
    LvGLUiStylePtr adhocStyle = LvGLUiStylePtr(new LvGLUiStyle(*this));
    err = adhocStyle->configure(aStyleSpecOrDefinition);
    if (Error::isOK(err)) {
      mAdhocStyles.push_back(adhocStyle);
      aStyleP = &(adhocStyle->mStyle);
      JsonObjectPtr o = aStyleSpecOrDefinition->get("selector");
      if (o) {
        aSelector = getSelectorByList(o->stringValue());
        if (aSelector==LV_STATE_DEFAULT) err = TextError::err("invalid selector '%s'", o->stringValue().c_str());
      }
    }
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
    lv_scr_load(screen->mElement);
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
    lv_disp_set_default(mDisplay); // make sure screens are created on the correct display
    err = addElements(o, NULL, true); // screens are just elements with no parent
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
  lv_disp_trig_activity(NULL);
  return ErrorPtr();
}


#if 0

ErrorPtr LvGLUi::setProperty(const string& aName, JsonObjectPtr aValue)
{
  ErrorPtr err;
  // check for themes
  if (aName=="themes") {
    for (int i = 0; i<aValue->arrayLength(); ++i) {
      JsonObjectPtr themeConfig = aValue->arrayGet(i);
      LvGLUiThemePtr th = LvGLUiThemePtr(new LvGLUiTheme(*this));
      th->configure(themeConfig);
      if (th->getName().empty()) return TextError::err("theme must have a 'name'");
      mThemes[th->getName()] = th;
    }
  }
  // check for styles
  else if (aName=="styles") {
    for (int i = 0; i<aValue->arrayLength(); ++i) {
      JsonObjectPtr styleConfig = aValue->arrayGet(i);
      LvGLUiStylePtr st = LvGLUiStylePtr(new LvGLUiStyle(*this));
      err = st->configure(styleConfig);
      if (Error::notOK(err)) return err;
      if (st->getName().empty()) return TextError::err("style must have a 'name'");
      mStyles[st->getName()] = st;
    }
  }
  // check for default theme
  else if (aName=="theme") {
    lv_theme_t* th = namedTheme(aValue->stringValue());
    if (th) {
      lv_display_set_theme(mDisplay, th);
    }
  }
  // check for screens
  else if (aName=="screens") {
    lv_disp_set_default(mDisplay); // make sure screens are created on the correct display
    addElements(aValue, NULL, true); // screens are just elements with no parent
  }
  // check for start screen to load
  else if (aName=="startscreen") {
    loadScreen(aValue->stringValue());
  }
  else if (aName=="resourceprefix") {
    mResourcePrefix = aValue->stringValue();
  }
  else if (aName=="dataresources") {
    mDataPathResources = aValue->boolValue();
  }
  // check for activation/deactivation scripts
  else if (aName=="activitytimeoutscript") {
    mActivityTimeoutScript.setSource(aValue->stringValue());
  }
  else if (aName=="activationscript") {
    mActivationScript.setSource(aValue->stringValue());
  }
  else {
    return inherited::setProperty(aName, aValue);
  }
  // simulate activity
  lv_disp_trig_activity(NULL);
  return ErrorPtr();
}

#endif

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
  lv_obj_send_event(o->element()->mElement, LV_EVENT_REFRESH, NULL);
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
  FUNC_DEF_W_ARG(configure, executable|structured),
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedLvglobjFunctionLookupP = NULL;

LVGLUiElementObj::LVGLUiElementObj(LVGLUiElementPtr aElement) :
  mElement(aElement)
{
  registerSharedLookup(sharedLvglobjFunctionLookupP, lvglobjFunctions);
}

#endif // ENABLE_LVGLUI_SCRIPT_FUNCS

#endif // ENABLE_LVGL
