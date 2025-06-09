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

#ifndef __p44utils__lvglui__
#define __p44utils__lvglui__


#include "lvgl.hpp"

#if ENABLE_LVGL

#include "jsonobject.hpp"

#if ENABLE_P44SCRIPT && !defined(ENABLE_LVGLUI_SCRIPT_FUNCS)
  #define ENABLE_LVGLUI_SCRIPT_FUNCS 1
#endif
#if ENABLE_LVGLUI_SCRIPT_FUNCS && !ENABLE_P44SCRIPT
  #error "ENABLE_P44SCRIPT required when ENABLE_LVGLUI_SCRIPT_FUNCS is set"
#endif

#if ENABLE_LVGLUI_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif



using namespace std;

namespace p44 {

  class LvGLUi;
  typedef boost::intrusive_ptr<LvGLUi> LvGLUiPtr;


  /// base class for any configurable object
  class LvGLUIObject : public P44Obj
  {
  protected:

    LvGLUi& mLvglui;
    string mName;

    /// handle setting a event handler
    /// @param aEventCode the event to handler
    /// @param aHandler the p44script handler for the event
    virtual ErrorPtr setEventHandler(lv_event_code_t aEventCode, JsonObjectPtr aHandler);

  public:

    LvGLUIObject(LvGLUi& aLvGLUI) : mLvglui(aLvGLUI) {};

    const string& getName() { return mName; };
    LvGLUi& getLvGLUi() { return mLvglui; };

    /// configure this object from json
    /// @note this might need to be overridden for objects that need to process
    ///   properties in a specific order
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig);

    /// handle setting a property
    /// @param aName the name of the property
    /// @param aValue the value of the property
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue);

  };




  /// a initialized theme (base theme + hue + font)
  class LvGLUiTheme : public LvGLUIObject
  {
    typedef LvGLUIObject inherited;

  public:

    lv_theme_t* mTheme;

    LvGLUiTheme(LvGLUi& aLvGLUI) : inherited(aLvGLUI), mTheme(NULL) {};

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig);

  };
  typedef boost::intrusive_ptr<LvGLUiTheme> LvGLUiThemePtr;
  typedef std::map<string, LvGLUiThemePtr> ThemeMap;


  /// customized style
  class LvGLUiStyle : public LvGLUIObject
  {
    typedef LvGLUIObject inherited;

  public:

    lv_style_t mStyle; ///< the LGVL style
    int32_t* mGridColsP; ///< grid columnts
    int32_t* mGridRowsP; ///< grid rows

    LvGLUiStyle(LvGLUi& aLvGLUI);
    virtual ~LvGLUiStyle();

    /// handle setting a property
    /// @param aName the name of the property
    /// @param aValue the value of the property
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<LvGLUiStyle> LvGLUiStylePtr;
  typedef std::map<string, LvGLUiStylePtr> StyleMap;
  typedef std::list<LvGLUiStylePtr> StyleList;


  // MARK: - element and container base classes

  class LVGLUiElement;
  class LvGLUiContainer;
  typedef boost::intrusive_ptr<LVGLUiElement> LVGLUiElementPtr;
  typedef boost::intrusive_ptr<LvGLUiContainer> LVGLUiContainerPtr;


  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  class LVGLUIEventHandler : public P44Obj
  {
  public:
    LVGLUiElement& mLVGLUIElement; ///< the element this handler is for
    P44Script::ScriptHost mEventScript; ///< script executed to process event

    LVGLUIEventHandler(LVGLUiElement& aElement, lv_event_code_t aEventCode, const string& aSource);
  };
  typedef boost::intrusive_ptr<LVGLUIEventHandler> LVGLUIEventHandlerPtr;
  #endif


  /// abstract base class for visible UI elements, wrapping a lv_obj
  class LVGLUiElement : public LvGLUIObject
  {
    friend class LvGLUi;

    typedef LvGLUIObject inherited;
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    typedef std::list<LVGLUIEventHandlerPtr> EventHandlersList;
    EventHandlersList mEventHandlers;
    LVGLUIEventHandlerPtr mRefreshEventHandler; // separate in case we want to call it directly
    #endif
    bool mHandlesEvents;

  public:

    lv_obj_t* mElement;
    LvGLUiContainer* mParentP;

    LVGLUiElement(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ~LVGLUiElement();

    lv_obj_t* lvParent();

    /// set a variable representing a property and return true if value has changed
    /// @param aTargetValue the variable to update
    /// @param aNewValue the new value
    /// @note returns true if variable is actually changed
    template<typename T> bool setProp(T &aTargetValue, T aNewValue)
    {
      if (aTargetValue!=aNewValue) {
        aTargetValue = aNewValue;
        return true; // changed value
      }
      return false; // not changed value
    };

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

    /// handle setting a property
    /// @param aName the name of the property
    /// @param aValue the value of the property
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;

    /// clear this element (and all of its named and unnamed children)
    virtual void clear();

    /// @return true if the wrapper object must be kept around (e.g. because it needs to handle events)
    virtual bool wrapperNeeded() { return !mEventHandlers.empty() || !getName().empty(); }; // simple objects need the wrapper only if they handle events or can be referenced by name

    /// @param aValue the value to set to the element (depends on element type)
    /// @param aAnimationTime if set>0, the value change will be animated
    virtual void setValue(int16_t aValue, uint16_t aAnimationTimeMs = 0) { /* NOP in base class */ }

    /// set a new text for an element
    void setText(const string &aNewText);

    /// get value
    /// @return current value of the control
    virtual int16_t getValue() { return 0; /* no value in base class */ }

    #if ENABLE_LVGLUI_SCRIPT_FUNCS

    /// run event script
    void runEventScript(lv_event_code_t aEventCode, P44Script::ScriptHost& aScriptCode);
    void scriptDone();

    /// set event handler
    virtual ErrorPtr setEventHandler(lv_event_code_t aEventCode, JsonObjectPtr aHandler) P44_OVERRIDE;

    #endif // ENABLE_LVGLUI_SCRIPT_FUNCS

  protected:

    virtual void setTextRaw(const string &aNewText) { /* NOP in base class */ }

    static const void* imgSrc(const string& aSource);

  };
  typedef std::map<string, LVGLUiElementPtr> ElementMap;
  typedef std::list<LVGLUiElementPtr> ElementList;


  /// abstract for a UI element that can create contained child objects from config
  class LvGLUiContainer : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
    friend class LvGLUi;

    ElementMap mNamedElements; ///< the contained elements that have a name because the need to be referencable
    ElementList mAnonymousElements; ///< the contained elements that need to be around after configuration because they are actionable

  public:

    LvGLUiContainer(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP) : inherited(aLvGLUI, aParentP) {};

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

    /// handle setting a property
    /// @param aName the name of the property
    /// @param aValue the value of the property
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;

    /// clear this element (and all of its named and unnamed children)
    virtual void clear() P44_OVERRIDE;

    /// @return true if the wrapper object must be kept around (e.g. because it needs to handle events)
    virtual bool wrapperNeeded() P44_OVERRIDE { return true; }; // containers always needs wrapper

  protected:

    ErrorPtr addElements(JsonObjectPtr aElementConfigArray, LvGLUiContainer* aParent, bool aContainerByDefault);

  };


  // MARK: - specific UI elements

  class LvGLUiPlain : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiPlain(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
  };


  class LvGLUiPanel : public LvGLUiContainer
  {
    typedef LvGLUiContainer inherited;
  public:
    LvGLUiPanel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
  };



  class LvGLUiImage : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
    string mImgSrc;
  public:
    LvGLUiImage(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
    virtual bool wrapperNeeded() P44_OVERRIDE { return true; }; // wrapper stores the image source, must be kept
};


  class LvGLUiLabel : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiLabel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
  };


  #if LV_USE_QRCODE
  class LvGLUiQRCode : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiQRCode(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
  };
  #endif


  class LvGLUiButton : public LvGLUiContainer
  {
    typedef LvGLUiContainer inherited;
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    P44Script::ScriptHost mOnPressScript;
    P44Script::ScriptHost mOnReleaseScript;
    #endif
    lv_obj_t *mLabel;
  public:
    LvGLUiButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ~LvGLUiButton();
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;
  protected:
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
  };


  class LvGLUiImgButton : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
    string relImgSrc;
    string prImgSrc;
    string tglPrImgSrc;
    string tglRelImgSrc;
    string inaImgSrc;
    bool imgsAssigned;
  public:
    LvGLUiImgButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;
    virtual bool wrapperNeeded() P44_OVERRIDE { return true; }; // wrapper stores the image sources, must be kept
  protected:
    static const void *imgBtnSrc(const string& aSource);
  };


  class LvGLUiBar : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiBar(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;
  protected:
    virtual int16_t getValue() P44_OVERRIDE { return lv_bar_get_value(mElement); }
    virtual void setValue(int16_t aValue, uint16_t aAnimationTimeMs = 0) P44_OVERRIDE;
  };


  #if LV_USE_SLIDER
  class LvGLUiSlider : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiSlider(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
    virtual ErrorPtr setProperty(const string& aName, JsonObjectPtr aValue) P44_OVERRIDE;
  protected:
    virtual int16_t getValue() P44_OVERRIDE;
    virtual void setValue(int16_t aValue, uint16_t aAnimationTimeMs = 0) P44_OVERRIDE;
  };
  #endif


  #if LV_USE_SWITCH
  class LvGLUiSwitch : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiSwitch(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP);
  protected:
    virtual int16_t getValue() P44_OVERRIDE;
    virtual void setValue(int16_t aValue, uint16_t aAnimationTimeMs = 0) P44_OVERRIDE;
  };
  #endif


  // MARK: - LvGLUi

  class LvGLUi : public LvGLUiContainer
  {
    typedef LvGLUiContainer inherited;

    lv_disp_t* mDisplay; ///< the display this gui appears on
    lv_obj_t* mEmptyScreen; ///< a programmatically created "screen" we can load when UI gets redefined

    StyleMap mStyles; ///< styles
    ThemeMap mThemes; ///< initialized themes (basic theme + hue + font)

    bool mDataPathResources; ///< look for resources also in data path
    string mResourcePrefix; ///< prefix for resource loading

    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    P44Script::ScriptMainContextPtr mScriptMainContext;
    P44Script::ScriptObjPtr mRepresentingObj;
    P44Script::ScriptHost mActivityTimeoutScript;
    P44Script::ScriptHost mActivationScript;
    #endif

  protected:

    virtual void clear() P44_OVERRIDE;

  public:

    LvGLUi();
    virtual ~LvGLUi();

    #if ENABLE_LVGLUI_SCRIPT_FUNCS

    /// set main context for all lvgl object level script executions in
    /// @param aScriptMainContext main context to execute lvgl object level scripts (sequentially among each other)
    void setScriptMainContext(P44Script::ScriptMainContextPtr aScriptMainContext);

    /// get the main script context (or create it on demand if not set by setScriptMainContext()
    /// @return the context that runs all lvgl ui event handlers (queued, one by one)
    P44Script::ScriptMainContextPtr getScriptMainContext();

    /// @return a singleton script object, representing this lvgl ui instance
    P44Script::ScriptObjPtr representingScriptObj();

    /// report activation / timeout of UI
    /// @note actual mechanism to detect UI usage or inactivity must be implemented on app level
    ///    This method is only to call respective scripts
    /// @param aActivated if set, this is an UI activation, otherwise a UI timeout
    void uiActivation(bool aActivated);

    #endif // ENABLE_LVGLUI_SCRIPT_FUNCS

    /// initialize for use with a specified display
    /// @param aDisplay the display to use
    void initForDisplay(lv_disp_t* aDisplay);

    /// @return the lv\_disp_t this UI runs on
    lv_disp_t* display() { return mDisplay; }

    /// clear current UI and set new config
    /// @param aConfig the new config for the UI
    ErrorPtr setConfig(JsonObjectPtr aConfig);

    /// configure this object from json
    /// @note overridden because GUI setup needs to be processed in specific order
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

    /// get named theme (from themes defined in config)
    /// @param aThemeName the name of the theme
    /// @return specified theme or NULL if not found
    lv_theme_t* namedTheme(const string aThemeName);

    /// get named style (custom as defined in config or built-in)
    /// @param aStyleName the name of the style
    /// @return specified style or NULL if not found
    lv_style_t* namedStyle(const string aStyleName);

    /// get named style from styles list or create ad-hoc style from definition
    /// @param aStyleSpecOrDefinition single string with the name and states of an existing style, or object defining an ad-hoc style and "states"
    /// @param aStyleP will be set to named or adhoc style (owned by this UI)
    /// @param aSelector state(s) and part(s) this style should apply to, default is LV_STATE_DEFAULT;
    /// @return true if a style could be found/created
    ErrorPtr namedStyle(JsonObjectPtr aStyleSpecOrDefinition, lv_style_t*& aStyleP, lv_style_selector_t& aSelector);

    /// get image file path, will possibly look up in different places (resources, data)
    /// @param aImageSpec a path or filename specifying an image
    /// @return absolute path to existing image file, or empty string if none of the possible places contain the file
    virtual string imagePath(const string aImageSpec);

    /// get image source specification by name
    /// @param aImageSpec a path specifying an image
    /// @note names containing dots will be considered file paths. Other texts are considered symbol names.
    ///    fallback is a text image label.
    /// @return image specification (file path or symbol)
    string namedImageSource(const string& aImageSpec);

    /// @param aElementPath dot separated absolute path beginning at root container, or dot-prefixed relative path
    ///   (.elem = one of my subelements, ..elem=a sibling (element in my parent's container), ...=grandparent, etc.)
    /// @param aOrigin the origin for relative paths
    /// @return requested element or NULL if none found
    LVGLUiElementPtr namedElement(string aElementPath, LVGLUiElementPtr aOrigin);

    /// load named screen and call its onrefreshscript
    /// @param aScreenName the name of the screen to load
    void loadScreen(const string aScreenName);

    /// set resource loading options
    /// @param aFromDataPath if set, non-absolute resource (image) file names are first looked up in datapath
    /// @param aPrefix if not empty and image spec does not start with "./", this is prepended to the image spec
    ///    in both data and resource paths
    void setResourceLoadOptions(bool aFromDataPath, const string aPrefix);

  };


  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  namespace P44Script {

    /// represents a object of a LvGLUI object hierarchy
    class LVGLUiElementObj : public StructuredLookupObject
    {
      friend class p44::LvGLUi;

      typedef P44Script::StructuredLookupObject inherited;
      LVGLUiElementPtr mElement;
    public:
      LVGLUiElementObj(LVGLUiElementPtr aElement);
      virtual string getAnnotation() const P44_OVERRIDE { return "lvglObj"; };
      LVGLUiElementPtr element() { return mElement; }
    };

  }
  #endif // ENABLE_LVGLUI_SCRIPT_FUNCS

} // namespace p44


#endif // ENABLE_LVGL

#endif /* __p44utils__lvglui__ */
