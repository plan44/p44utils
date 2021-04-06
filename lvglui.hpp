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
#ifndef LVGLUI_LEGCACY_FUNCTIONS
  #define LVGLUI_LEGCACY_FUNCTIONS 0
#endif


#if ENABLE_LVGLUI_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif



using namespace std;

namespace p44 {

  class LvGLUi;

  /// base class for any configurable object
  class LvGLUIObject : public P44Obj
  {
  protected:

    LvGLUi& lvglui;
    string name;

  public:

    LvGLUIObject(LvGLUi& aLvGLUI) : lvglui(aLvGLUI) {};

    const string& getName() { return name; };
    LvGLUi& getLvGLUi() { return lvglui; };

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig);

  };




  /// a initialized theme (base theme + hue + font)
  class LvGLUiTheme : public LvGLUIObject
  {
    typedef LvGLUIObject inherited;

  public:

    lv_theme_t* theme;

    LvGLUiTheme(LvGLUi& aLvGLUI) : inherited(aLvGLUI), theme(NULL) {};

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<LvGLUiTheme> LvGLUiThemePtr;
  typedef std::map<string, LvGLUiThemePtr> ThemeMap;


  /// customized style
  class LvGLUiStyle : public LvGLUIObject
  {
    typedef LvGLUIObject inherited;

  public:

    lv_style_t style; ///< the LGVL style

    LvGLUiStyle(LvGLUi& aLvGLUI);

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<LvGLUiStyle> LvGLUiStylePtr;
  typedef std::map<string, LvGLUiStylePtr> StyleMap;
  typedef std::list<LvGLUiStylePtr> StyleList;


  // MARK: - element and container base classes

  class LVGLUiElement;
  class LvGLUiContainer;
  typedef boost::intrusive_ptr<LVGLUiElement> LVGLUiElementPtr;
  typedef boost::intrusive_ptr<LvGLUiContainer> LVGLUiContainerPtr;


  /// abstract base class for visible UI elements, wrapping a lv_obj
  class LVGLUiElement : public LvGLUIObject
  {
    friend class LvGLUi;

    typedef LvGLUIObject inherited;
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    P44Script::ScriptSource onEventScript; ///< script executed to process otherwise unhandled lvgl events on this element
    P44Script::ScriptSource onRefreshScript; ///< script executed to specifically process "refresh" event
    #endif
    bool handlesEvents;

  public:

    lv_obj_t* element;
    LvGLUiContainer* parentP;

    LVGLUiElement(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
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
    /// @param aParent parent object, or NULL if none
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

    /// let object handle an event
    virtual void handleEvent(lv_event_t aEvent);

    /// clear this element (and all of its named and unnamed children)
    virtual void clear();

    /// @return true if the wrapper object must be kept around (e.g. because it needs to handle events)
    virtual bool wrapperNeeded() { return handlesEvents || !getName().empty(); }; // simple objects need the wrapper only if they handle events or can be referenced by name

    /// @param aValue the value to set to the element (depends on element type)
    /// @param aAnimationTime if set>0, the value change will be animated
    virtual void setValue(int16_t aValue, uint16_t aAnimationTimeMs = 0) { /* NOP in base class */ }

    /// set a new text for an element
    void setText(const string &aNewText);

    /// get value
    /// @return current value of the control
    virtual int16_t getValue() { return 0; /* no value in base class */ }

    /// run event script
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    void runEventScript(lv_event_t aEvent, P44Script::ScriptSource& aScriptCode);
    #endif

  protected:

    void installEventHandler();
    virtual void setTextRaw(const string &aNewText) { /* NOP in base class */ }


    static const void* imgSrc(const string& aSource);

  };
  typedef std::map<string, LVGLUiElementPtr> ElementMap;
  typedef std::list<LVGLUiElementPtr> ElementList;


  /// abstract class for lv_cont and similar objects with layout features
  class LvGLUiLayoutContainer : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiLayoutContainer(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) : inherited(aLvGLUI, aParentP, aTemplate) {};
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  };


  /// abstract for a UI element that can create contained child objects from config
  class LvGLUiContainer : public LvGLUiLayoutContainer
  {
    typedef LvGLUiLayoutContainer inherited;
    friend class LvGLUi;

    ElementMap namedElements; ///< the contained elements that have a name because the need to be referencable
    ElementList anonymousElements; ///< the contained elements that need to be around after configuration because they are actionable

  public:

    LvGLUiContainer(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) : inherited(aLvGLUI, aParentP, aTemplate) {};

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    /// @param aParent parent object, or NULL if none
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

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
    LvGLUiPlain(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
  };


  class LvGLUiPanel : public LvGLUiContainer
  {
    typedef LvGLUiContainer inherited;
  public:
    LvGLUiPanel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
  };



  class LvGLUiImage : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
    string imgSrc;
  public:
    LvGLUiImage(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
    virtual bool wrapperNeeded() P44_OVERRIDE { return true; }; // wrapper stores the image source, must be kept
};


  class LvGLUiLabel : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiLabel(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
  };


  class LvGLUiButton : public LvGLUiLayoutContainer
  {
    typedef LvGLUiLayoutContainer inherited;
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    P44Script::ScriptSource onPressScript;
    P44Script::ScriptSource onReleaseScript;
    #endif
    lv_obj_t *label;
  public:
    LvGLUiButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  protected:
    virtual void handleEvent(lv_event_t aEvent) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
  };


  class LvGLUiImgButton : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    P44Script::ScriptSource onPressScript;
    P44Script::ScriptSource onReleaseScript;
    #endif
    string relImgSrc;
    string prImgSrc;
    string tglPrImgSrc;
    string tglRelImgSrc;
    string inaImgSrc;
    bool imgsAssigned;
  public:
    LvGLUiImgButton(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
    virtual bool wrapperNeeded() P44_OVERRIDE { return true; }; // wrapper stores the image sources, must be kept
  protected:
    virtual void handleEvent(lv_event_t aEvent) P44_OVERRIDE;
    static const void *imgBtnSrc(const string& aSource);
  };


  class LvGLUiBarBase : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiBarBase(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate) : inherited(aLvGLUI, aParentP, aTemplate) {};
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  protected:
    virtual void setValue(int16_t aValue, uint16_t aAnimationTimeMs = 0) P44_OVERRIDE;
  };


  class LvGLUiBar : public LvGLUiBarBase
  {
    typedef LvGLUiBarBase inherited;
  public:
    LvGLUiBar(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual int16_t getValue() P44_OVERRIDE { return lv_bar_get_value(element); }
  };


  class LvGLUiSlider : public LvGLUiBarBase
  {
    typedef LvGLUiBarBase inherited;
    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    P44Script::ScriptSource onChangeScript;
    P44Script::ScriptSource onReleaseScript;
    #endif
  public:
    LvGLUiSlider(LvGLUi& aLvGLUI, LvGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  protected:
    virtual void handleEvent(lv_event_t aEvent) P44_OVERRIDE;
    virtual int16_t getValue() P44_OVERRIDE { return lv_slider_get_value(element); }
  };



  // MARK: - LvGLUiScriptContext

  #warning "%%% delete"
  /*
  class LvGLUiScriptContext : public ScriptExecutionContext
  {
    typedef ScriptExecutionContext inherited;
    friend class LvGLUi;

    LvGLUi& lvglui;
    int currentEvent; // -1 means none
    LVGLUiElementPtr currentElement; // the element that triggered the current script

  public:

    LvGLUiScriptContext(LvGLUi& aLvGLUI) : lvglui(aLvGLUI), currentEvent(-1) {};

    /// script context specific functions
    virtual bool evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult) P44_OVERRIDE;
  };
  */

  // MARK: - LvGLUi


  class LvGLUi : public LvGLUiContainer
  {
    typedef LvGLUiContainer inherited;

    /* %%%
    class LvGLUiScriptRequest
    {
      %%%
      friend class LvGLUi;

      LvGLUiScriptRequest(lv_event_t aEvent, LVGLUiElementPtr aElement, const string &aScriptCode, P44ObjPtr aCallerContext) :
      event(aEvent), element(aElement), scriptCode(aScriptCode), callerContext(aCallerContext) {};
      lv_event_t event;
      LVGLUiElementPtr element;
      string scriptCode;
      P44ObjPtr callerContext;
    };

    typedef std::list<LvGLUiScriptRequest> ScriptRequestList;
    ScriptRequestList scriptRequests;
    */

    lv_disp_t *display; ///< the display this gui appears on

    StyleMap styles; ///< custom styles
    StyleList adhocStyles; ///< keeps ad hoc styles
    ThemeMap themes; ///< initialized themes (basic theme + hue + font)

    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    P44Script::ScriptMainContextPtr mScriptMainContext;
    #endif

  protected:

    virtual void clear() P44_OVERRIDE;

  public:

    LvGLUi();

    #if ENABLE_LVGLUI_SCRIPT_FUNCS
    /// set main context for all lvgl object level script executions in
    /// @param aScriptMainContext main context to execute lvgl object level scripts (sequentially among each other)
    void setScriptMainContext(P44Script::ScriptMainContextPtr aScriptMainContext);
    #endif

    /// initialize for use with a specified display
    /// @param aDisplay the display to use
    void initForDisplay(lv_disp_t* aDisplay);

    /// clear current UI and set new config
    /// @param aConfig the new config for the UI
    ErrorPtr setConfig(JsonObjectPtr aConfig);

    /// can be used to re-configure UI later (e.g. to add more screens) without clearing existing UI hierarchy
    /// @param aConfig configuration to apply to the global lvgl container, e.g. new screen or style
    /// @return ok or error
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
    /// @param aStyleNameOrDefinition single string with the name of an existing style, or object defining an ad-hoc style
    /// @param aDefaultToPlain if true, and style does not exist or cannot be defined ad-hoc, return the plain style instead of NULL
    /// @return specified existig or ad-hoc style, NULL (or plain if aDefaultToPlain is set) if specified style cannot be returned
    lv_style_t* namedOrAdHocStyle(JsonObjectPtr aStyleNameOrDefinition, bool aDefaultToPlain);

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

    #if ENABLE_LVGLUI_SCRIPT_FUNCS

    /// queue event script to run (when others scripts are done)
    /// @param aEvent the LittevGL event that causes the script to execute
    /// @param aScriptCode the script code string to execute
    /// @param aCallerContext optional context object for the execution (can be used by custom function implementations)
    void queueEventScript(lv_event_t aEvent, LVGLUiElementPtr aElement, P44Script::ScriptSource& aScriptCode, P44ObjPtr aCallerContext = NULL);

  private:

    void scriptDone(LVGLUiElementPtr aElement);

    #endif // ENABLE_P44SCRIPT

  };


  #if ENABLE_LVGLUI_SCRIPT_FUNCS
  namespace P44Script {

    /// represents a object of a LvGLUI object hierarchy
    class LVGLUiElementObj : public P44Script::StructuredLookupObject
    {
      friend class p44::LvGLUi;

      typedef P44Script::StructuredLookupObject inherited;
      LVGLUiElementPtr mElement;
      int currentEvent; // -1 means none
    public:
      LVGLUiElementObj(LVGLUiElementPtr aElement);
      virtual string getAnnotation() const P44_OVERRIDE { return "lvglObj"; };
      LVGLUiElementPtr element() { return mElement; }
    };

    /// represents the global objects related to http
    class LvGLUiLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
      LvGLUi& mLvGLUi;
    public:
      LvGLUi* lvglui() { return &mLvGLUi; };
      LvGLUiLookup(LvGLUi &aLvGLUi);
    };

  }
  #endif


} // namespace p44


#endif // ENABLE_LVGL

#endif /* __p44utils__lvglui__ */
