//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

static lv_font_t* getFontByName(const string aFontName)
{
  if (aFontName=="roboto12")
    return &lv_font_roboto_12;
  else if (aFontName=="roboto16")
    return &lv_font_roboto_16;
  else if (aFontName=="roboto22")
    return &lv_font_roboto_22;
  else if (aFontName=="roboto28")
    return &lv_font_roboto_28;
  else
    return NULL;
}


static lv_layout_t getLayoutByName(const string aLayoutName)
{
  if (aLayoutName=="center") return LV_LAYOUT_CENTER;
  else if (aLayoutName=="column_left") return LV_LAYOUT_COL_L;
  else if (aLayoutName=="column_middle") return LV_LAYOUT_COL_M;
  else if (aLayoutName=="column_right") return LV_LAYOUT_COL_R;
  else if (aLayoutName=="row_top") return LV_LAYOUT_ROW_T;
  else if (aLayoutName=="row_middle") return LV_LAYOUT_ROW_M;
  else if (aLayoutName=="row_bottom") return LV_LAYOUT_ROW_B;
  else if (aLayoutName=="pretty") return LV_LAYOUT_PRETTY;
  else if (aLayoutName=="grid") return LV_LAYOUT_GRID;
  else return LV_LAYOUT_OFF;
}


static lv_fit_t getAutoFitByName(const string aAutoFitName)
{
  if (aAutoFitName=="tight") return LV_FIT_TIGHT;
  else if (aAutoFitName=="flood") return LV_FIT_FLOOD;
  else if (aAutoFitName=="fill") return LV_FIT_FILL;
  else return LV_FIT_NONE;
}



static lv_style_t* getStyleByName(const string aStyleName)
{
  if (aStyleName=="scr")
    return &lv_style_scr;
  else if (aStyleName=="transp")
    return &lv_style_transp;
  else if (aStyleName=="scr")
    return &lv_style_scr;
  else if (aStyleName=="transp")
    return &lv_style_transp;
  else if (aStyleName=="transp_fit")
    return &lv_style_transp_fit;
  else if (aStyleName=="transp_tight")
    return &lv_style_transp_tight;
  else if (aStyleName=="plain")
    return &lv_style_plain;
  else if (aStyleName=="plain_color")
    return &lv_style_plain_color;
  else if (aStyleName=="pretty")
    return &lv_style_pretty;
  else if (aStyleName=="pretty_color")
    return &lv_style_pretty_color;
  else if (aStyleName=="btn_rel")
    return &lv_style_btn_rel;
  else if (aStyleName=="btn_pr")
    return &lv_style_btn_pr;
  else if (aStyleName=="btn_tgl_rel")
    return &lv_style_btn_tgl_rel;
  else if (aStyleName=="btn_tgl_pr")
    return &lv_style_btn_tgl_pr;
  else if (aStyleName=="btn_ina")
    return &lv_style_btn_ina;
  else
    return NULL;
}


static const char* getSymbolByName(const string aSymbolName)
{
  if (aSymbolName=="audio") return LV_SYMBOL_AUDIO;
  else if (aSymbolName=="video") return LV_SYMBOL_VIDEO;
  else if (aSymbolName=="list") return LV_SYMBOL_LIST;
  else if (aSymbolName=="ok") return LV_SYMBOL_OK;
  else if (aSymbolName=="close") return LV_SYMBOL_CLOSE;
  else if (aSymbolName=="power") return LV_SYMBOL_POWER;
  else if (aSymbolName=="settings") return LV_SYMBOL_SETTINGS;
  else if (aSymbolName=="trash") return LV_SYMBOL_TRASH;
  else if (aSymbolName=="home") return LV_SYMBOL_HOME;
  else if (aSymbolName=="download") return LV_SYMBOL_DOWNLOAD;
  else if (aSymbolName=="drive") return LV_SYMBOL_DRIVE;
  else if (aSymbolName=="refresh") return LV_SYMBOL_REFRESH;
  else if (aSymbolName=="mute") return LV_SYMBOL_MUTE;
  else if (aSymbolName=="volume_mid") return LV_SYMBOL_VOLUME_MID;
  else if (aSymbolName=="volume_max") return LV_SYMBOL_VOLUME_MAX;
  else if (aSymbolName=="image") return LV_SYMBOL_IMAGE;
  else if (aSymbolName=="edit") return LV_SYMBOL_EDIT;
  else if (aSymbolName=="prev") return LV_SYMBOL_PREV;
  else if (aSymbolName=="play") return LV_SYMBOL_PLAY;
  else if (aSymbolName=="pause") return LV_SYMBOL_PAUSE;
  else if (aSymbolName=="stop") return LV_SYMBOL_STOP;
  else if (aSymbolName=="next") return LV_SYMBOL_NEXT;
  else if (aSymbolName=="eject") return LV_SYMBOL_EJECT;
  else if (aSymbolName=="left") return LV_SYMBOL_LEFT;
  else if (aSymbolName=="right") return LV_SYMBOL_RIGHT;
  else if (aSymbolName=="plus") return LV_SYMBOL_PLUS;
  else if (aSymbolName=="minus") return LV_SYMBOL_MINUS;
  else if (aSymbolName=="warning") return LV_SYMBOL_WARNING;
  else if (aSymbolName=="shuffle") return LV_SYMBOL_SHUFFLE;
  else if (aSymbolName=="up") return LV_SYMBOL_UP;
  else if (aSymbolName=="down") return LV_SYMBOL_DOWN;
  else if (aSymbolName=="loop") return LV_SYMBOL_LOOP;
  else if (aSymbolName=="directory") return LV_SYMBOL_DIRECTORY;
  else if (aSymbolName=="upload") return LV_SYMBOL_UPLOAD;
  else if (aSymbolName=="call") return LV_SYMBOL_CALL;
  else if (aSymbolName=="cut") return LV_SYMBOL_CUT;
  else if (aSymbolName=="copy") return LV_SYMBOL_COPY;
  else if (aSymbolName=="save") return LV_SYMBOL_SAVE;
  else if (aSymbolName=="charge") return LV_SYMBOL_CHARGE;
  else if (aSymbolName=="bell") return LV_SYMBOL_BELL;
  else if (aSymbolName=="keyboard") return LV_SYMBOL_KEYBOARD;
  else if (aSymbolName=="gps") return LV_SYMBOL_GPS;
  else if (aSymbolName=="file") return LV_SYMBOL_FILE;
  else if (aSymbolName=="wifi") return LV_SYMBOL_WIFI;
  else if (aSymbolName=="battery_full") return LV_SYMBOL_BATTERY_FULL;
  else if (aSymbolName=="battery_3") return LV_SYMBOL_BATTERY_3;
  else if (aSymbolName=="battery_2") return LV_SYMBOL_BATTERY_2;
  else if (aSymbolName=="battery_1") return LV_SYMBOL_BATTERY_1;
  else if (aSymbolName=="battery_empty") return LV_SYMBOL_BATTERY_EMPTY;
  else if (aSymbolName=="bluetooth") return LV_SYMBOL_BLUETOOTH;
  else return "";
}


static lv_color_t colorFromWebColor(const string aWebColor)
{
  size_t i = 0;
  size_t n = aWebColor.size();
  if (n>0 && aWebColor[0]=='#') { i++; n--; } // skip optional #
  uint32_t h;
  int r=0, g=0, b=0;
  if (sscanf(aWebColor.c_str()+i, "%x", &h)==1) {
    if (n<=4) {
      // short form RGB
      r = (h>>8)&0xF; r |= r<<4;
      g = (h>>4)&0xF; g |= g<<4;
      b = (h>>0)&0xF; b |= b<<4;
    }
    else {
      // long form RRGGBB
      r = (h>>16)&0xFF;
      g = (h>>8)&0xFF;
      b = (h>>0)&0xFF;
    }
  }
  return lv_color_make(r,g,b);
}


static lv_border_part_t borderPartFromList(const string aBorderParts)
{
  const char* p = aBorderParts.c_str();
  string part;
  lv_border_part_t parts = LV_BORDER_NONE;
  while (nextPart(p, part, ',')) {
    if (part=="bottom")
      parts |= LV_BORDER_BOTTOM;
    else if (part=="top")
      parts |= LV_BORDER_TOP;
    else if (part=="left")
      parts |= LV_BORDER_LEFT;
    else if (part=="right")
      parts |= LV_BORDER_RIGHT;
    else if (part=="full")
      parts |= LV_BORDER_FULL;
    else if (part=="internal")
      parts |= LV_BORDER_INTERNAL;
  }
  return parts;
}


static lv_align_t alignModeByName(const string aAlignMode)
{
  const char *p = aAlignMode.c_str();
  bool in = true;
  bool top = false;
  bool mid = false;
  bool bottom = false;
  bool left = false;
  bool right = false;
  string tok;
  while (nextPart(p, tok, ',')) {
    if (tok=="top") top = true;
    else if (tok=="mid") mid = true;
    else if (tok=="bottom") bottom = true;
    else if (tok=="left") left = true;
    else if (tok=="right") right = true;
    else if (tok=="in") in = true;
    else if (tok=="out") in = false;
  }
  if (in && top && left)
    return LV_ALIGN_IN_TOP_LEFT;
  if (in && top && mid)
    return LV_ALIGN_IN_TOP_MID;
  if (in && top && right)
    return LV_ALIGN_IN_TOP_RIGHT;
  if (in && bottom && left)
    return LV_ALIGN_IN_BOTTOM_LEFT;
  if (in && bottom && mid)
    return LV_ALIGN_IN_BOTTOM_MID;
  if (in && bottom && right)
    return LV_ALIGN_IN_BOTTOM_RIGHT;
  if (in && left && mid)
    return LV_ALIGN_IN_LEFT_MID;
  if (in && right && mid)
    return LV_ALIGN_IN_RIGHT_MID;
  if (!in && top && left)
    return LV_ALIGN_OUT_TOP_LEFT;
  if (!in && top && mid)
    return LV_ALIGN_OUT_TOP_MID;
  if (!in && top && right)
    return LV_ALIGN_OUT_TOP_RIGHT;
  if (!in && bottom && left)
    return LV_ALIGN_OUT_BOTTOM_LEFT;
  if (!in && bottom && mid)
    return LV_ALIGN_OUT_BOTTOM_MID;
  if (!in && bottom && right)
    return LV_ALIGN_OUT_BOTTOM_RIGHT;
  if (!in && left && top)
    return LV_ALIGN_OUT_LEFT_TOP;
  if (!in && left && mid)
    return LV_ALIGN_OUT_LEFT_MID;
  if (!in && left && bottom)
    return LV_ALIGN_OUT_LEFT_BOTTOM;
  if (!in && right && top)
    return LV_ALIGN_OUT_RIGHT_TOP;
  if (!in && right && mid)
    return LV_ALIGN_OUT_RIGHT_MID;
  if (!in && right && bottom)
    return LV_ALIGN_OUT_RIGHT_BOTTOM;
  else
    return LV_ALIGN_CENTER;
}


static const char *eventName(lv_event_t aEvent)
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
    case LV_EVENT_DRAG_BEGIN: etxt = "drag_begin"; break;
    case LV_EVENT_DRAG_END: etxt = "drag_end"; break;
    case LV_EVENT_DRAG_THROW_BEGIN: etxt = "drag_throw"; break; // end of drag with momentum
    case LV_EVENT_KEY: etxt = "key"; break;
    case LV_EVENT_FOCUSED: etxt = "focused"; break;
    case LV_EVENT_DEFOCUSED: etxt = "defocused"; break;
    case LV_EVENT_VALUE_CHANGED: etxt = "changed"; break; // The object's value has changed (i.e. slider moved)
    case LV_EVENT_INSERT: etxt = "insert"; break;
    case LV_EVENT_REFRESH: etxt = "refresh"; break;
    case LV_EVENT_APPLY: etxt = "apply"; break; // "Ok", "Apply" or similar specific button has clicked
    case LV_EVENT_CANCEL: etxt = "cancel"; break; // "Close", "Cancel" or similar specific button has clicked
    case LV_EVENT_DELETE: etxt = "delete"; break; // Object is being deleted
  }
  return etxt;
}



// MARK: - LvGLUIObject


ErrorPtr LvGLUIObject::configure(JsonObjectPtr aConfig)
{
  JsonObjectPtr o;
  if (aConfig->get("name", o)) {
    name = o->stringValue();
  }
  return ErrorPtr();
}


// MARK: - LvGLUiTheme

ErrorPtr LvGLUiTheme::configure(JsonObjectPtr aConfig)
{
  JsonObjectPtr o;
  uint16_t hue = 0;
  lv_font_t* font = NULL;
  string themeName;
  if (aConfig->get("hue", o)) {
    hue = o->int32Value();
  }
  if (aConfig->get("font", o)) {
    font = getFontByName(o->stringValue());
  }
  if (aConfig->get("theme", o)) {
    themeName = o->stringValue();
  }
  // (re-)init theme
  if (themeName=="material") {
    theme = lv_theme_material_init(hue, font);
  }
  else if (themeName=="alien") {
    theme = lv_theme_alien_init(hue, font);
  }
  else if (themeName=="mono") {
    theme = lv_theme_mono_init(hue, font);
  }
  else if (themeName=="nemo") {
    theme = lv_theme_nemo_init(hue, font);
  }
  else if (themeName=="night") {
    theme = lv_theme_night_init(hue, font);
  }
  else if (themeName=="zen") {
    theme = lv_theme_zen_init(hue, font);
  }
  else {
    theme = lv_theme_default_init(hue, font);
  }
  return inherited::configure(aConfig);
}


// MARK: - LvGLUiStyle

LvGLUiStyle::LvGLUiStyle(LvGLUi& aLvGLUI) : inherited(aLvGLUI)
{
  lv_style_copy(&style, &lv_style_plain); // base on plain by default
}


ErrorPtr LvGLUiStyle::configure(JsonObjectPtr aConfig)
{
  JsonObjectPtr o;
  if (aConfig->get("template", o)) {
    lv_style_t* s = lvglui.namedStyle(o->stringValue());
    if (!s) return TextError::err("unknown style '%s' as template", o->stringValue().c_str());
    lv_style_copy(&style, s);
  }
  // set style properties
  if (aConfig->get("glass", o)) {
    style.glass = o->boolValue();
  }
  // - body
  if (aConfig->get("color", o)) {
    style.body.main_color = colorFromWebColor(o->stringValue());
    // also set gradient color. Use "main_color" to set main color alone
    style.body.grad_color = style.body.main_color;
  }
  if (aConfig->get("main_color", o)) {
    style.body.main_color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("gradient_color", o)) {
    style.body.grad_color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("radius", o)) {
    if (o->stringValue()=="circle")
      style.body.radius = LV_RADIUS_CIRCLE;
    else
      style.body.radius = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("alpha", o)) {
    style.body.opa = (lv_opa_t)o->int32Value();
  }
  // - border
  if (aConfig->get("border_color", o)) {
    style.body.border.color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("border_width", o)) {
    style.body.border.width = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("border_alpha", o)) {
    style.body.border.opa = (lv_opa_t)o->int32Value();
  }
  if (aConfig->get("border_parts", o)) {
    style.body.border.part = borderPartFromList(o->stringValue());
  }
  // - shadow
  if (aConfig->get("shadow_color", o)) {
    style.body.shadow.color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("shadow_width", o)) {
    style.body.shadow.width = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("shadow_full", o)) {
    style.body.shadow.type = o->boolValue() ? LV_SHADOW_FULL : LV_SHADOW_BOTTOM;
  }
  // - paddings
  if (aConfig->get("padding_top", o)) {
    style.body.padding.top = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("padding_bottom", o)) {
    style.body.padding.bottom = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("padding_left", o)) {
    style.body.padding.left = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("padding_right", o)) {
    style.body.padding.right = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("padding_inner", o)) {
    style.body.padding.inner = (lv_coord_t)o->int32Value();
  }
  // - text
  if (aConfig->get("text_color", o)) {
    style.text.color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("text_selection_color", o)) {
    style.text.sel_color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("font", o)) {
    style.text.font = getFontByName(o->stringValue());
  }
  if (aConfig->get("text_letter_space", o)) {
    style.text.letter_space = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("text_line_space", o)) {
    style.text.line_space = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("text_alpha", o)) {
    style.text.opa = (lv_opa_t)o->int32Value();
  }
  // - image
  if (aConfig->get("image_color", o)) {
    style.image.color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("image_recoloring", o)) {
    style.image.intense = (lv_opa_t)o->int32Value();
  }
  if (aConfig->get("image_alpha", o)) {
    style.image.opa = (lv_opa_t)o->int32Value();
  }
  // - line
  if (aConfig->get("line_color", o)) {
    style.line.color = colorFromWebColor(o->stringValue());
  }
  if (aConfig->get("line_width", o)) {
    style.line.width = (lv_coord_t)o->int32Value();
  }
  if (aConfig->get("line_alpha", o)) {
    style.line.opa = (lv_opa_t)o->int32Value();
  }
  if (aConfig->get("line_rounded", o)) {
    style.line.rounded = (lv_opa_t)o->boolValue();
  }
  return inherited::configure(aConfig);
}


// MARK - Element Factory

static LVGLUiElementPtr createElement(LvGLUi& aLvGLUI, JsonObjectPtr aConfig, LvGLUiContainer* aParentP, bool aContainerByDefault)
{
  LVGLUiElementPtr elem;
  JsonObjectPtr o;
  lv_obj_t* tmpl = NULL;
  string tn;
  if (aConfig->get("type", o)) {
    tn = o->stringValue();
  }
  if (aConfig->get("template", o)) {
    // reference an existing named element to copy from (sibling)
    LVGLUiElementPtr templateElem = aLvGLUI.namedElement(o->stringValue(), aParentP);
    if (templateElem) tmpl = templateElem->element;
  }
  // now create according to type
  if (tn=="panel") {
    elem = LVGLUiElementPtr(new LvGLUiPanel(aLvGLUI, aParentP, tmpl));
  }
  else if (tn=="image") {
    elem = LVGLUiElementPtr(new LvGLUiImage(aLvGLUI, aParentP, tmpl));
  }
  else if (tn=="label") {
    elem = LVGLUiElementPtr(new LvGLUiLabel(aLvGLUI, aParentP, tmpl));
  }
  else if (tn=="button") {
    if (aConfig->get("image"))
      elem = LVGLUiElementPtr(new LvGLUiImgButton(aLvGLUI, aParentP, tmpl));
    else
      elem = LVGLUiElementPtr(new LvGLUiButton(aLvGLUI, aParentP, tmpl));
  }
  else if (tn=="image_button") {
    elem = LVGLUiElementPtr(new LvGLUiImgButton(aLvGLUI, aParentP, tmpl));
  }
  else if (tn=="slider") {
    elem = LVGLUiElementPtr(new LvGLUiSlider(aLvGLUI, aParentP, tmpl));
  }
  else {
    if (aContainerByDefault)
      elem = LVGLUiElementPtr(new LvGLUiPanel(aLvGLUI, aParentP, tmpl));
    else
      elem = LVGLUiElementPtr(new LvGLUiPlain(aLvGLUI, aParentP, tmpl));
  }
  return elem;
}


// MARK: - LVGLUiElement

const void* LVGLUiElement::imgSrc(const string& aSource)
{
  if (aSource.empty()) return NULL;
  return aSource.c_str();
}



LVGLUiElement::LVGLUiElement(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI),
  parentP(aParentP),
  element(NULL),
  handlesEvents(false)
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  ,onEventScript(scriptbody+regular, "onEvent")
  ,onRefreshScript(scriptbody+regular, "onRefresh")
  #endif
{
}


lv_obj_t* LVGLUiElement::lvParent()
{
  return parentP ? parentP->element : NULL;
}


LVGLUiElement::~LVGLUiElement()
{
  clear();
}


void LVGLUiElement::clear()
{
  if (element) {
    lv_obj_del(element); // delete element and all of its children on the lvgl level
    element = NULL;
  }
}



ErrorPtr LVGLUiElement::configure(JsonObjectPtr aConfig)
{
  if (!element) return TextError::err("trying to configure non-existing lv_obj");
  JsonObjectPtr o;
  LVGLUiElementPtr alignRef;
  lv_coord_t align_dx = 0;
  lv_coord_t align_dy = 0;
  bool align_middle = false;
  // common properties
  if (aConfig->get("x", o)) {
    lv_obj_set_x(element, o->int32Value());
  }
  if (aConfig->get("y", o)) {
    lv_obj_set_y(element, o->int32Value());
  }
  if (aConfig->get("dx", o)) {
    lv_obj_set_width(element, o->int32Value());
  }
  if (aConfig->get("dy", o)) {
    lv_obj_set_height(element, o->int32Value());
  }
  if (aConfig->get("alignto", o)) {
    alignRef = lvglui.namedElement(o->stringValue(), parentP); // sibling
  }
  if (aConfig->get("align_dx", o)) {
    align_dx = o->int32Value();
  }
  if (aConfig->get("align_dy", o)) {
    align_dy = o->int32Value();
  }
  if (aConfig->get("align_middle", o)) {
    align_middle = o->boolValue();
  }
  if (aConfig->get("align", o)) {
    if (align_middle)
      lv_obj_align_origo(element, alignRef->element, alignModeByName(o->stringValue()), align_dx, align_dy);
    else
      lv_obj_align(element, alignRef->element, alignModeByName(o->stringValue()), align_dx, align_dy);
  }
  if (aConfig->get("style", o)) {
    lv_style_t* style = lvglui.namedOrAdHocStyle(o, true);
    if (style) lv_obj_set_style(element, style);
  }
  if (aConfig->get("hidden", o)) {
    lv_obj_set_hidden(element, o->boolValue());
  }
  if (aConfig->get("click", o)) {
    lv_obj_set_click(element, o->boolValue());
  }
  if (aConfig->get("extended_click", o)) {
    int ext = o->int32Value();
    lv_obj_set_ext_click_area(element, ext, ext, ext, ext);
  }
  // generic content change
  if (aConfig->get("value", o)) {
    setValue(o->int32Value(), 0); // w/o animation. Use script function setValue() for animated changes
  }
  if (aConfig->get("text", o)) {
    setText(o->stringValue());
  }
  // events
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aConfig->get("onevent", o)) {
    onEventScript.setSource(o->stringValue());
    installEventHandler();
  }
  if (aConfig->get("onrefresh", o)) {
    onRefreshScript.setSource(o->stringValue());
    installEventHandler();
  }
  #endif
  return inherited::configure(aConfig);
}

static void elementEventCallback(lv_obj_t * obj, lv_event_t event)
{
  LVGLUiElement *eventSource = static_cast<LVGLUiElement*>(lv_obj_get_user_data(obj));
  if (eventSource) {
    eventSource->handleEvent(event);
  }
}


void LVGLUiElement::installEventHandler()
{
  if (!handlesEvents) {
    handlesEvents = true;
    // set user data
    lv_obj_set_user_data(element, (void *)this);
    // set callback
    lv_obj_set_event_cb(element, &elementEventCallback);
  }
}


#if ENABLE_LVGLUI_SCRIPT_FUNCS

void LVGLUiElement::runEventScript(lv_event_t aEvent, ScriptSource& aScriptCode)
{
  lvglui.queueEventScript(aEvent, this, aScriptCode);
}

#endif


void LVGLUiElement::handleEvent(lv_event_t aEvent)
{
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aEvent==LV_EVENT_REFRESH && !onRefreshScript.empty()) {
    runEventScript(aEvent, onRefreshScript);
  }
  else if (!onEventScript.empty()) {
    runEventScript(aEvent, onEventScript);
  }
  #endif
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



// MARK: - LvGLUiLayoutContainer

ErrorPtr LvGLUiLayoutContainer::configure(JsonObjectPtr aConfig)
{
  JsonObjectPtr o;
  // common properties
  if (aConfig->get("layout", o)) {
    lv_cont_set_layout(element, getLayoutByName(o->stringValue()));
  }
  if (aConfig->get("fit", o)) {
    lv_cont_set_fit(element, getAutoFitByName(o->stringValue()));
  }
  if (aConfig->get("fit_horizontal", o)) {
    lv_cont_set_fit4(element, o->int32Value(), o->int32Value(), lv_cont_get_fit_top(element), lv_cont_get_fit_bottom(element));
  }
  if (aConfig->get("fit_vertical", o)) {
    lv_cont_set_fit4(element, lv_cont_get_fit_left(element), lv_cont_get_fit_right(element), o->int32Value(), o->int32Value());
  }
  return inherited::configure(aConfig);
}


// MARK: - LVGLUiContainer

void LvGLUiContainer::clear()
{
  namedElements.clear();
  anonymousElements.clear();
  inherited::clear();
}


ErrorPtr LvGLUiContainer::addElements(JsonObjectPtr aElementConfigArray, LvGLUiContainer* aParent, bool aContainerByDefault)
{
  ErrorPtr err;
  for (int i = 0; i<aElementConfigArray->arrayLength(); ++i) {
    JsonObjectPtr elementConfig = aElementConfigArray->arrayGet(i);
    LVGLUiElementPtr uielement = createElement(lvglui, elementConfig, aParent, aContainerByDefault);
    if (!uielement || !uielement->element) {
      err = TextError::err("unknown/invalid element type: %s", elementConfig->c_strValue());
      break;
    }
    err = uielement->configure(elementConfig);
    if (Error::notOK(err)) break;
    FOCUSLOG("Created Element '%s' from: %s", uielement->getName().c_str(), elementConfig->c_strValue());
    // add to named elements if it has a name
    if (!uielement->getName().empty()) {
      namedElements[uielement->getName()] = uielement;
    }
    else if (!aParent || uielement->wrapperNeeded()) {
      anonymousElements.push_back(uielement);
    }
    else {
      // this element does not need a wrapper, and has a parent which will release this child's memory
      // so we just need to make sure disposing of the wrapper will not delete the lv_obj
      uielement->element = NULL; // cut lv_obj from the wrapper
    }
  }
  return err;
}


ErrorPtr LvGLUiContainer::configure(JsonObjectPtr aConfig)
{
  // configure basics
  ErrorPtr err = inherited::configure(aConfig);
  if (Error::isOK(err)) {
    // check for elements
    JsonObjectPtr o;
    if (aConfig->get("elements", o)) {
      addElements(o, this, false); // normal elements have a parent, and default elements are plain elements
    }
  }
  return err;
}


// MARK: - LvGLUiPlain - simple object with no child layout

LvGLUiPlain::LvGLUiPlain(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate)
{
  element = lv_obj_create(lvParent(), aTemplate);
}


// MARK: - LvGLUiPanel - object with layout features for contained children

LvGLUiPanel::LvGLUiPanel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate)
{
  element = lv_cont_create(lvParent(), aTemplate);
}


// MARK: - LvGLUiImage

LvGLUiImage::LvGLUiImage(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate)
{
  element = lv_img_create(lvParent(), aTemplate);
}


ErrorPtr LvGLUiImage::configure(JsonObjectPtr aConfig)
{
  // configure params
  JsonObjectPtr o;
  if (aConfig->get("autosize", o)) {
    lv_img_set_auto_size(element, o->boolValue());
  }
  if (aConfig->get("src", o)) {
    if (setProp(imgSrc, lvglui.namedImageSource(o->stringValue())))
      lv_img_set_src(element, imgSrc.c_str());
  }
  if (aConfig->get("symbol", o)) {
    lv_img_set_src(element, getSymbolByName(o->stringValue()));
  }
  if (aConfig->get("offset_x", o)) {
    lv_img_set_offset_x(element, o->int32Value());
  }
  if (aConfig->get("offset_y", o)) {
    lv_img_set_offset_y(element, o->int32Value());
  }
  return inherited::configure(aConfig);
}


void LvGLUiImage::setTextRaw(const string &aNewText)
{
  string imgTxt = LV_SYMBOL_DUMMY;
  imgTxt.append(aNewText);
  lv_img_set_src(element, imgTxt.c_str());
}



// MARK: - LvGLUiLabel

LvGLUiLabel::LvGLUiLabel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate)
{
  element = lv_label_create(lvParent(), aTemplate);
}


ErrorPtr LvGLUiLabel::configure(JsonObjectPtr aConfig)
{
  // configure params
  JsonObjectPtr o;
  if (aConfig->get("longmode", o)) {
    string lm = o->stringValue();
    if (lm=="expand") lv_label_set_long_mode(element, LV_LABEL_LONG_EXPAND);
    else if (lm=="break") lv_label_set_long_mode(element, LV_LABEL_LONG_BREAK);
    else if (lm=="dot") lv_label_set_long_mode(element, LV_LABEL_LONG_DOT);
    else if (lm=="scroll") lv_label_set_long_mode(element, LV_LABEL_LONG_SROLL);
    else if (lm=="circularscroll") lv_label_set_long_mode(element, LV_LABEL_LONG_SROLL_CIRC);
    else if (lm=="crop") lv_label_set_long_mode(element, LV_LABEL_LONG_CROP);
  }
  if (aConfig->get("text_align", o)) {
    string ta = o->stringValue();
    if (ta=="left") lv_label_set_align(element, LV_LABEL_ALIGN_LEFT);
    else if (ta=="center") lv_label_set_align(element, LV_LABEL_ALIGN_CENTER);
    else if (ta=="right") lv_label_set_align(element, LV_LABEL_ALIGN_RIGHT);
  }
  if (aConfig->get("background", o)) {
    lv_label_set_body_draw(element, o->boolValue());
  }
  if (aConfig->get("inline_colors", o)) {
    lv_label_set_recolor(element, o->boolValue());
  }
  return inherited::configure(aConfig);
}

void LvGLUiLabel::setTextRaw(const string &aNewText)
{
  lv_label_set_text(element, aNewText.c_str());
}



// MARK: - LvGLUiButton

LvGLUiButton::LvGLUiButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate),
  label(NULL)
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  ,onPressScript(scriptbody+regular, "onPress")
  ,onReleaseScript(scriptbody+regular, "onRelease")
  #endif
{
  element = lv_btn_create(lvParent(), aTemplate);
}


static void configureButtonCommon(LvGLUi &aUi, lv_obj_t *btn, JsonObjectPtr aConfig)
{
  JsonObjectPtr o;
  if (aConfig->get("released_style", o)) {
    lv_style_t *style = aUi.namedOrAdHocStyle(o, true);
    if (style) lv_btn_set_style(btn, LV_BTN_STYLE_REL, style);
  }
  if (aConfig->get("pressed_style", o)) {
    lv_style_t *style = aUi.namedOrAdHocStyle(o, true);
    if (style) lv_btn_set_style(btn, LV_BTN_STYLE_PR, style);
  }
  if (aConfig->get("on_style", o)) {
    lv_style_t *style = aUi.namedOrAdHocStyle(o, true);
    if (style) lv_btn_set_style(btn, LV_BTN_STYLE_TGL_PR, style);
  }
  if (aConfig->get("off_style", o)) {
    lv_style_t *style = aUi.namedOrAdHocStyle(o, true);
    if (style) lv_btn_set_style(btn, LV_BTN_STYLE_TGL_REL, style);
  }
  if (aConfig->get("disabled_style", o)) {
    lv_style_t *style = aUi.namedOrAdHocStyle(o, true);
    if (style) lv_btn_set_style(btn, LV_BTN_STYLE_INA, style);
  }
  if (aConfig->get("state", o)) {
    string st = o->stringValue();
    lv_btn_state_t sta = LV_BTN_STATE_REL; // default to released
    if (st=="pressed") sta = LV_BTN_STATE_PR;
    else if (st=="on") sta = LV_BTN_STATE_TGL_PR;
    else if (st=="off") sta = LV_BTN_STATE_TGL_REL;
    else if (st=="inactive") sta = LV_BTN_STATE_INA;
    lv_btn_set_state(btn, sta);
  }
}


ErrorPtr LvGLUiButton::configure(JsonObjectPtr aConfig)
{
  // configure params
  JsonObjectPtr o;
  if (aConfig->get("toggle", o)) {
    lv_btn_set_toggle(element, o->boolValue());
  }
  if (aConfig->get("ink_in", o)) {
    lv_btn_set_ink_in_time(element, o->int32Value());
  }
  if (aConfig->get("ink_wait", o)) {
    lv_btn_set_ink_wait_time(element, o->int32Value());
  }
  if (aConfig->get("ink_out", o)) {
    lv_btn_set_ink_out_time(element, o->int32Value());
  }
  if (aConfig->get("label", o)) {
    // convenience for text-labelled buttons
    label = lv_label_create(element, NULL);
    setText(o->stringValue());
  }
  // common button+imgBtn properties
  configureButtonCommon(lvglui, element, aConfig);
  // event handling
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aConfig->get("onpress", o)) {
    onPressScript.setSource(o->stringValue());
    installEventHandler();
  }
  if (aConfig->get("onrelease", o)) {
    onReleaseScript.setSource(o->stringValue());
    installEventHandler();
  }
  #endif
  return inherited::configure(aConfig);
}


void LvGLUiButton::handleEvent(lv_event_t aEvent)
{
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aEvent==LV_EVENT_PRESSED && !onPressScript.empty()) {
    runEventScript(aEvent, onPressScript);
  }
  else if (aEvent==LV_EVENT_RELEASED && !onReleaseScript.empty()) {
    runEventScript(aEvent, onReleaseScript);
  }
  else
  #endif
  {
    inherited::handleEvent(aEvent);
  }
}


void LvGLUiButton::setTextRaw(const string &aNewText)
{
  if (label) lv_label_set_text(label, aNewText.c_str());
}


// MARK: - LvGLUiImgButton

const void* LvGLUiImgButton::imgBtnSrc(const string& aSource)
{
  const void* src = LVGLUiElement::imgSrc(aSource);
  if (src) {
    // avoid symbols in image buttons (these only work in normal images)
    if (lv_img_src_get_type(src)==LV_IMG_SRC_SYMBOL) {
      src = NULL;
    }
  }
  return src;
}


LvGLUiImgButton::LvGLUiImgButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate),
  imgsAssigned(false)
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  ,onPressScript(scriptbody+regular, "onPress")
  ,onReleaseScript(scriptbody+regular, "onRelease")
  #endif
{
  element = lv_imgbtn_create(lvParent(), aTemplate);
}


ErrorPtr LvGLUiImgButton::configure(JsonObjectPtr aConfig)
{
  // configure params
  ErrorPtr err;
  JsonObjectPtr o;
  if (aConfig->get("toggle", o)) {
    lv_imgbtn_set_toggle(element, o->boolValue());
  }
  // common button+imgBtn properties
  configureButtonCommon(lvglui, element, aConfig);
  // images
  if (aConfig->get("released_image", o) || aConfig->get("image", o)) {
    if (setProp(relImgSrc, lvglui.namedImageSource(o->stringValue())))
      lv_imgbtn_set_src(element, LV_BTN_STATE_REL, relImgSrc.c_str());
  }
  if (aConfig->get("pressed_image", o)) {
    if (setProp(prImgSrc, lvglui.namedImageSource(o->stringValue())))
      lv_imgbtn_set_src(element, LV_BTN_STATE_PR, prImgSrc.c_str());
  }
  if (aConfig->get("on_image", o)) {
    if (setProp(tglPrImgSrc, lvglui.namedImageSource(o->stringValue())))
      lv_imgbtn_set_src(element, LV_BTN_STATE_TGL_PR, tglPrImgSrc.c_str());
  }
  if (aConfig->get("off_image", o)) {
    if (setProp(tglRelImgSrc, lvglui.namedImageSource(o->stringValue())))
      lv_imgbtn_set_src(element, LV_BTN_STATE_TGL_REL, tglRelImgSrc.c_str());
  }
  if (aConfig->get("disabled_image", o)) {
    if (setProp(inaImgSrc, lvglui.namedImageSource(o->stringValue())))
      lv_imgbtn_set_src(element, LV_BTN_STATE_INA, inaImgSrc.c_str());
  }
  // - make sure all states have an image, default to released image
  if (!relImgSrc.empty() && !imgsAssigned) {
    if (prImgSrc.empty()) { prImgSrc = relImgSrc; lv_imgbtn_set_src(element, LV_BTN_STATE_PR, prImgSrc.c_str()); }
    if (tglPrImgSrc.empty()) { tglPrImgSrc = relImgSrc; lv_imgbtn_set_src(element, LV_BTN_STATE_TGL_PR, tglPrImgSrc.c_str()); }
    if (tglRelImgSrc.empty()) { tglRelImgSrc = relImgSrc; lv_imgbtn_set_src(element, LV_BTN_STATE_TGL_REL, tglRelImgSrc.c_str()); }
    if (inaImgSrc.empty()) { inaImgSrc = relImgSrc; lv_imgbtn_set_src(element, LV_BTN_STATE_INA, inaImgSrc.c_str()); }
    imgsAssigned = true;
  }
  // event handling
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aConfig->get("onpress", o)) {
    onPressScript.setSource(o->stringValue());
    installEventHandler();
  }
  if (aConfig->get("onrelease", o)) {
    onReleaseScript.setSource(o->stringValue());
    installEventHandler();
  }
  #endif
  return inherited::configure(aConfig);
}


void LvGLUiImgButton::handleEvent(lv_event_t aEvent)
{
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aEvent==LV_EVENT_PRESSED && !onPressScript.empty()) {
    runEventScript(aEvent, onPressScript);
  }
  else if (aEvent==LV_EVENT_RELEASED && !onReleaseScript.empty()) {
    runEventScript(aEvent, onReleaseScript);
  }
  else
  #endif
  {
    inherited::handleEvent(aEvent);
  }
}



// MARK: - LvGLUiBarBase


ErrorPtr LvGLUiBarBase::configure(JsonObjectPtr aConfig)
{
  // configure params
  JsonObjectPtr o;
  if (aConfig->get("indicator_style", o)) {
    lv_style_t *style = lvglui.namedOrAdHocStyle(o, true);
    if (style) lv_bar_set_style(element, LV_BAR_STYLE_INDIC, style);
  }
  if (aConfig->get("min", o)) {
    lv_bar_set_range(element, o->int32Value(), lv_bar_get_max_value(element));
  }
  if (aConfig->get("max", o)) {
    lv_bar_set_range(element, lv_bar_get_min_value(element), o->int32Value());
  }
  return inherited::configure(aConfig);
}


void LvGLUiBarBase::setValue(int16_t aValue, uint16_t aAnimationTimeMs)
{
  if (aAnimationTimeMs>0) {
    lv_bar_set_anim_time(element, aAnimationTimeMs);
  }
  lv_bar_set_value(element, aValue, aAnimationTimeMs>0 ? LV_ANIM_ON : LV_ANIM_OFF);
}



// MARK: - LvGLUiBar

LvGLUiBar::LvGLUiBar(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate)
{
  element = lv_bar_create(lvParent(), aTemplate);
}



// MARK: - LvGLUiSlider

LvGLUiSlider::LvGLUiSlider(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) :
  inherited(aLvGLUI, aParentP, aTemplate)
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  ,onChangeScript(scriptbody+regular, "onChange")
  ,onReleaseScript(scriptbody+regular, "onRelease")
  #endif
{
  element = lv_slider_create(lvParent(), aTemplate);
}


ErrorPtr LvGLUiSlider::configure(JsonObjectPtr aConfig)
{
  // configure params
  JsonObjectPtr o;
  if (aConfig->get("knob_style", o)) {
    lv_style_t *style = lvglui.namedOrAdHocStyle(o, true);
    if (style) lv_slider_set_style(element, LV_SLIDER_STYLE_KNOB, style);
  }
  if (aConfig->get("knob_inside", o)) {
    lv_slider_set_knob_in(element, o->boolValue());
  }
  if (aConfig->get("indicator_sharp", o)) {
    lv_slider_set_sharp_indic_edge(element, o->boolValue());
  }
  if (aConfig->get("min", o)) {
    int min = o->int32Value();
    if (aConfig->get("max", o)) {
      int max = o->int32Value();
      lv_slider_set_range(element, min, max);
    }
  }
  // event handling
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aConfig->get("onchange", o)) {
    onChangeScript.setSource(o->stringValue());
    installEventHandler();
  }
  if (aConfig->get("onrelease", o)) {
    onReleaseScript.setSource(o->stringValue());
    installEventHandler();
  }
  #endif
  return inherited::configure(aConfig);
}


void LvGLUiSlider::handleEvent(lv_event_t aEvent)
{
  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  if (aEvent==LV_EVENT_VALUE_CHANGED && !onChangeScript.empty()) {
    runEventScript(aEvent, onChangeScript);
  }
  else if (aEvent==LV_EVENT_RELEASED && !onReleaseScript.empty()) {
    runEventScript(aEvent, onReleaseScript);
  }
  else
  #endif
  {
    inherited::handleEvent(aEvent);
  }
}



// MARK: - LvGLUi

static LvGLUi* gLvgluiP = NULL;

LvGLUi::LvGLUi() :
  inherited(*this, NULL, NULL)
{
  name = "LvGLUi";
  gLvgluiP = this;
}


void LvGLUi::clear()
{
  lv_img_cache_invalidate_src(NULL); // clear image cache
  inherited::clear();
  styles.clear();
  adhocStyles.clear();
  themes.clear();
}


void LvGLUi::initForDisplay(lv_disp_t* aDisplay)
{
  clear();
  display = aDisplay;
}


ErrorPtr LvGLUi::setConfig(JsonObjectPtr aConfig)
{
  clear();
  return configure(aConfig);
}


lv_theme_t* LvGLUi::namedTheme(const string aThemeName)
{
  ThemeMap::iterator pos = themes.find(aThemeName);
  if (pos!=themes.end())
    return pos->second->theme;
  return NULL;
}


lv_style_t* LvGLUi::namedStyle(const string aStyleName)
{
  // try custom styles first
  StyleMap::iterator pos = styles.find(aStyleName);
  if (pos!=styles.end())
    return &pos->second->style;
  // try built-in styles
  return getStyleByName(aStyleName);
}


lv_style_t* LvGLUi::namedOrAdHocStyle(JsonObjectPtr aStyleNameOrDefinition, bool aDefaultToPlain)
{
  if (aStyleNameOrDefinition->isType(json_type_string)) {
    return namedStyle(aStyleNameOrDefinition->stringValue());
  }
  else if (aStyleNameOrDefinition->isType(json_type_object)) {
    // temporary object to parse it
    LvGLUiStylePtr adhocStyle = LvGLUiStylePtr(new LvGLUiStyle(*this));
    adhocStyle->configure(aStyleNameOrDefinition);
    adhocStyles.push_back(adhocStyle);
    return &adhocStyle->style;
  }
  return aDefaultToPlain ? &lv_style_plain : NULL; // default or none
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
      aOrigin = aOrigin->parentP;
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
      ElementMap::iterator pos = cont->namedElements.find(elemname);
      if (pos!=cont->namedElements.end()) {
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
  LVGLUiElementPtr screen = namedElement(aScreenName, &lvglui);
  if (screen) {
    lv_scr_load(screen->element);
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    lvglui.queueEventScript(LV_EVENT_REFRESH, screen, screen->onRefreshScript);
    #endif
  }
}


ErrorPtr LvGLUi::configure(JsonObjectPtr aConfig)
{
  JsonObjectPtr o;
  ErrorPtr err;
  // check for themes
  if (aConfig->get("themes", o)) {
    for (int i = 0; i<o->arrayLength(); ++i) {
      JsonObjectPtr themeConfig = o->arrayGet(i);
      LvGLUiThemePtr th = LvGLUiThemePtr(new LvGLUiTheme(*this));
      th->configure(themeConfig);
      if (th->getName().empty()) return TextError::err("theme must have a 'name'");
      themes[th->getName()] = th;
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
      styles[st->getName()] = st;
    }
  }
  // check for default theme
  if (aConfig->get("theme", o)) {
    lv_theme_t* th = namedTheme(o->stringValue());
    if (th) {
      lv_theme_set_current(th);
    }
  }
  // check for screens
  if (aConfig->get("screens", o)) {
    lv_disp_set_default(display); // make sure screens are created on the correct display
    addElements(o, NULL, true); // screens are just elements with no parent
  }
  // check for start screen to load
  if (aConfig->get("startscreen", o)) {
    loadScreen(o->stringValue());
  }
  // simulate activity
  lv_disp_trig_activity(NULL);
  return ErrorPtr();
}


string LvGLUi::imagePath(const string aImageSpec)
{
  string f = Application::sharedApplication()->dataPath(aImageSpec);
  if (access(f.c_str(), R_OK)>=0) return f;
  f = Application::sharedApplication()->resourcePath(aImageSpec);
  if (access(f.c_str(), R_OK)>=0) return f;
  return "";
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


void LvGLUi::setScriptMainContext(ScriptMainContextPtr aScriptMainContext)
{
  mScriptMainContext = aScriptMainContext;
}

void LvGLUi::queueEventScript(lv_event_t aEvent, LVGLUiElementPtr aElement, P44Script::ScriptSource& aScriptCode, P44ObjPtr aCallerContext)
{
  LOG(LOG_INFO, "--- Starting/queuing action script for LVGLUiElement '%s'", aElement ? aElement->getName().c_str() : "<none>");
  aScriptCode.setSharedMainContext(mScriptMainContext);
  LVGLUiElementObj* eventThreadObj = NULL;
  if (aElement) {
    eventThreadObj = new LVGLUiElementObj(aElement);
    eventThreadObj->currentEvent = aEvent;
  }
  aScriptCode.run(regular|queue|concurrently, boost::bind(&LvGLUi::scriptDone, this, aElement), eventThreadObj, Infinite);
}

void LvGLUi::scriptDone(LVGLUiElementPtr aElement)
{
  LOG(LOG_INFO, "--- Finished action script for LVGLUiElement '%s'", aElement ? aElement->getName().c_str() : "<none>");
}



// findobj(elementpath)
static const BuiltInArgDesc findobj_args[] = { { text } };
static const size_t findobj_numargs = sizeof(findobj_args)/sizeof(BuiltInArgDesc);
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


// parent()
static void parent_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  LVGLUiElement* parent = o->element()->parentP;
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
static const BuiltInArgDesc setvalue_args[] = { { numeric }, { numeric|optionalarg } };
static const size_t setvalue_numargs = sizeof(setvalue_args)/sizeof(BuiltInArgDesc);
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
static const BuiltInArgDesc settext_args[] = { { text } };
static const size_t settext_numargs = sizeof(settext_args)/sizeof(BuiltInArgDesc);
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
  lv_event_send(o->element()->element, LV_EVENT_REFRESH, NULL);
  f->finish(o); // return myself for chaining calls
}


// showScreen(<screenname>)
static const BuiltInArgDesc showscreen_args[] = { { text } };
static const size_t showscreen_numargs = sizeof(showscreen_args)/sizeof(BuiltInArgDesc);
static void showscreen_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  o->element()->getLvGLUi().loadScreen(f->arg(0)->stringValue());
  f->finish();
}


// set(propertyname, newvalue)   convenience function to set (configure) a single property
static const BuiltInArgDesc set_args[] = { { text }, { any } };
static const size_t set_numargs = sizeof(set_args)/sizeof(BuiltInArgDesc);
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
static const BuiltInArgDesc configure_args[] = { { text|json } };
static const size_t configure_numargs = sizeof(configure_args)/sizeof(BuiltInArgDesc);
static void configure_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  JsonObjectPtr cfgJSON;
  ErrorPtr err;
  #if SCRIPTING_JSON_SUPPORT
  if (f->arg(0)->hasType(json)) {
    // is already a JSON value, use it as-is
    cfgJSON = f->arg(0)->jsonValue();
  }
  else
  #endif
  {
    // JSON from string (or file if we have a JSON app)
    string cfgText = f->arg(0)->stringValue();
    #if LVGLUI_LEGCACY_FUNCTIONS
    string k,v;
    if (*cfgText.c_str()!='{' && keyAndValue(cfgText, k, v, '=')) {
      // legacy key=value string
      cfgJSON = JsonObject::newObj();
      cfgJSON->add(k.c_str(), cfgJSON->newString(v));
    }
    else
    #endif // LVGLUI_LEGCACY_FUNCTIONS
    {
      // literal json or filename
      #if ENABLE_JSON_APPLICATION
      cfgJSON = Application::jsonObjOrResource(cfgText, &err);
      #else
      cfgJSON = JsonObject::objFromText(cfgText.c_str(), -1, &err);
      #endif
    }
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


#if LVGLUI_LEGCACY_FUNCTIONS

// event()
static void event_func(BuiltinFunctionContextPtr f)
{
  LVGLUiElementObj* o = dynamic_cast<LVGLUiElementObj*>(f->thisObj().get());
  assert(o);
  // TODO: implement
  #warning "%%% tdb: get and return event threadvar"
  f->finish();
}

#endif


static const BuiltinMemberDescriptor lvglobjFunctions[] = {
  { "findobj", executable|object, findobj_numargs, findobj_args, &findobj_func },
  { "parent", executable|object, 0, NULL, &parent_func },
  { "value", executable|numeric, 0, NULL, &value_func },
  { "setvalue", executable|null, setvalue_numargs, setvalue_args, &setvalue_func },
  { "settext", executable|null, settext_numargs, settext_args, &settext_func },
  { "refresh", executable|null, 0, NULL, &refresh_func },
  { "showscreen", executable|null, showscreen_numargs, showscreen_args, &showscreen_func },
  { "set", executable|null, set_numargs, set_args, &set_func },
  { "configure", executable|null, configure_numargs, configure_args, &configure_func },
  #if LVGLUI_LEGCACY_FUNCTIONS
  { "event", executable|text, 0, NULL, &event_func },
  #endif
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedLvglobjFunctionLookupP = NULL;

LVGLUiElementObj::LVGLUiElementObj(LVGLUiElementPtr aElement) :
  mElement(aElement)
  #if LVGLUI_LEGCACY_FUNCTIONS
  ,currentEvent(-1) // means none
  #endif
{
  if (sharedLvglobjFunctionLookupP==NULL) {
    sharedLvglobjFunctionLookupP = new BuiltInMemberLookup(lvglobjFunctions);
    sharedLvglobjFunctionLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(sharedLvglobjFunctionLookupP);
}



#if LVGLUI_LEGCACY_FUNCTIONS

// TODO: implement
#warning "%%% tbd"

#endif


static ScriptObjPtr lvgl_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite)
{
  LvGLUiLookup* l = dynamic_cast<LvGLUiLookup*>(&aMemberLookup);
  LVGLUiElement* root = l->lvglui();
  if (!root) return new AnnotatedNullValue("no lvgl");
  return new LVGLUiElementObj(root);
}



static const BuiltinMemberDescriptor lvgluiGlobals[] = {
  #if LVGLUI_LEGCACY_FUNCTIONS
  // deprecated, backwards compatibility
  { "event", executable|text, lgcy_event_numargs, lgcy_event_args, &lgcy_event_func },
  { "value", executable|numeric, lgcy_value_numargs, lgcy_value_args, &lgcy_value_func },
  { "setvalue", executable|null, lgcy_setvalue_numargs, lgcy_setvalue_args, &lgcy_setvalue_func },
  { "settext", executable|null, lgcy_settext_numargs, lgcy_settext_args, &lgcy_settext_func },
  { "refresh", executable|null, lgcy_refresh_numargs, lgcy_refresh_args, &lgcy_refresh_func },
  { "showscreen", executable|null, lgcy_showscreen_numargs, lgcy_showscreen_args, &lgcy_showscreen_func },
  { "configure", executable|null, lgcy_configure_numargs, lgcy_configure_args, &lgcy_configure_func },
  #endif // LVGLUI_LEGCACY_FUNCTIONS
  { "lvgl", builtinmember, 0, NULL, (BuiltinFunctionImplementation)&lvgl_accessor }, // Note: correct '.accessor=&lrg_accessor' form does not work with OpenWrt g++, so need ugly cast here
  { NULL } // terminator
};

LvGLUiLookup::LvGLUiLookup(LvGLUi &aLvGLUi) :
  mLvGLUi(aLvGLUi),
  inherited(lvgluiGlobals)
{
}

#endif // ENABLE_LVGLUI_SCRIPT_FUNCS

#endif // ENABLE_LVGL
