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

#ifndef __p44utils__uielements__
#define __p44utils__uielements__


#include "lvgl.hpp"

#if ENABLE_LVGL

#include "jsonobject.hpp"
#include "expressions.hpp"


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

    const string &getName() { return name; };

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
  class LVGLUiContainer;
  typedef boost::intrusive_ptr<LVGLUiElement> LVGLUiElementPtr;
  typedef boost::intrusive_ptr<LVGLUiContainer> LVGLUiContainerPtr;


  /// abstract base class for visible UI elements, wrapping a lv_obj
  class LVGLUiElement : public LvGLUIObject
  {
    friend class LvGLUi;

    typedef LvGLUIObject inherited;
    string onEventScript; ///< script executed to process otherwise unhandled lvgl events on this element
    string onRefreshScript; ///< script executed to specifically process "refresh" event
    bool handlesEvents;

  public:

    lv_obj_t* element;
    LVGLUiContainer* parentP;

    LVGLUiElement(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
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
    virtual void setValue(int16_t aValue, uint16_t aAnimationTime = 0) { /* NOP in base class */ }

    /// set a new text for an element
    void setText(const string &aNewText);

    /// get value
    /// @return current value of the control
    virtual int16_t getValue() { return 0; /* no value in base class */ }

    /// run event script
    void runEventScript(lv_event_t aEvent, const string &aScriptCode);

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
    LvGLUiLayoutContainer(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate) : inherited(aLvGLUI, aParentP, aTemplate) {};
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  };


  /// abstract for a UI element that can create contained child objects from config
  class LVGLUiContainer : public LvGLUiLayoutContainer
  {
    typedef LvGLUiLayoutContainer inherited;
    friend class LvGLUi;

    ElementMap namedElements; ///< the contained elements that have a name because the need to be referencable
    ElementList anonymousElements; ///< the contained elements that need to be around after configuration because they are actionable

  public:

    LVGLUiContainer(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate) : inherited(aLvGLUI, aParentP, aTemplate) {};

    /// configure this object from json
    /// @param aConfig JSON object containing configuration propertyname/values
    /// @param aParent parent object, or NULL if none
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

    /// clear this element (and all of its named and unnamed children)
    virtual void clear() P44_OVERRIDE;

    /// @return true if the wrapper object must be kept around (e.g. because it needs to handle events)
    virtual bool wrapperNeeded() P44_OVERRIDE { return true; }; // containers always needs wrapper

  protected:

    ErrorPtr addElements(JsonObjectPtr aElementConfigArray, LVGLUiContainer* aParent, bool aContainerByDefault);

  };


  // MARK: - specific UI elements

  class LvGLUiPlain : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiPlain(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
  };


  class LvGLUiPanel : public LVGLUiContainer
  {
    typedef LVGLUiContainer inherited;
  public:
    LvGLUiPanel(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
  };



  class LvGLUiImage : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
    string imgSrc;
  public:
    LvGLUiImage(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
    virtual bool wrapperNeeded() P44_OVERRIDE { return true; }; // wrapper stores the image source, must be kept
};


  class LvGLUiLabel : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
  public:
    LvGLUiLabel(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
  };


  class LvGLUiButton : public LvGLUiLayoutContainer
  {
    typedef LvGLUiLayoutContainer inherited;
    string onPressScript;
    string onReleaseScript;
    lv_obj_t *label;
  public:
    LvGLUiButton(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  protected:
    virtual void handleEvent(lv_event_t aEvent) P44_OVERRIDE;
    virtual void setTextRaw(const string &aNewText) P44_OVERRIDE;
  };


  class LvGLUiImgButton : public LVGLUiElement
  {
    typedef LVGLUiElement inherited;
    string onPressScript;
    string onReleaseScript;
    string relImgSrc;
    string prImgSrc;
    string tglPrImgSrc;
    string tglRelImgSrc;
    string inaImgSrc;
    bool imgsAssigned;
  public:
    LvGLUiImgButton(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
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
    LvGLUiBarBase(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate) : inherited(aLvGLUI, aParentP, aTemplate) {};
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  protected:
    virtual void setValue(int16_t aValue, uint16_t aAnimationTime = 0) P44_OVERRIDE;
  };


  class LvGLUiBar : public LvGLUiBarBase
  {
    typedef LvGLUiBarBase inherited;
  public:
    LvGLUiBar(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual int16_t getValue() P44_OVERRIDE { return lv_bar_get_value(element); }
  };


  class LvGLUiSlider : public LvGLUiBarBase
  {
    typedef LvGLUiBarBase inherited;
    string onChangeScript;
    string onReleaseScript;
  public:
    LvGLUiSlider(LvGLUi& aLvGLUI, LVGLUiContainer* aParentP, lv_obj_t *aTemplate);
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;
  protected:
    virtual void handleEvent(lv_event_t aEvent) P44_OVERRIDE;
    virtual int16_t getValue() P44_OVERRIDE { return lv_slider_get_value(element); }
  };



  // MARK: - LvGLUiScriptContext

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


  // MARK: - LvGLUi


  class LvGLUi : public LVGLUiContainer
  {
    typedef LVGLUiContainer inherited;

    class LvGLUiScriptRequest
    {
      friend class LvGLUi;

      LvGLUiScriptRequest(lv_event_t aEvent, LVGLUiElementPtr aElement, const string &aScriptCode) :
      event(aEvent), element(aElement), scriptCode(aScriptCode) {};
      lv_event_t event;
      LVGLUiElementPtr element;
      string scriptCode;
    };

    typedef std::list<LvGLUiScriptRequest> ScriptRequestList;
    ScriptRequestList scriptRequests;

    lv_disp_t *display; ///< the display this gui appears on

    StyleMap styles; ///< custom styles
    StyleList adhocStyles; ///< keeps ad hoc styles
    ThemeMap themes; ///< initialized themes (basic theme + hue + font)

  protected:

    virtual void clear() P44_OVERRIDE;

  public:

    LvGLUiScriptContext uiScriptContext;

    LvGLUi();

    /// initialize for use with a specified display
    void initForDisplay(lv_disp_t* aDisplay);

    /// clear current UI and set new config
    ErrorPtr setConfig(JsonObjectPtr aConfig);

    /// can be used to re-configure UI later (e.g. to add more screens) without clearing existing UI hierarchy
    virtual ErrorPtr configure(JsonObjectPtr aConfig) P44_OVERRIDE;

    /// get named theme (from themes defined in config)
    lv_theme_t* namedTheme(const string aThemeName);

    /// get named style (custom as defined in config or built-in)
    lv_style_t* namedStyle(const string aStyleName);

    /// get named style from styles list or create ad-hoc style from definition
    lv_style_t* namedOrAdHocStyle(JsonObjectPtr aStyleNameOrDefinition, bool aDefaultToPlain);

    /// get image file path, will possibly look up in different places
    /// @param aImageSpec a path specifying an image
    virtual string imagePath(const string aImageSpec);

    /// get image source specification by name
    /// @note names containing dots will be considered file paths. Other texts are considered symbol names.
    ///    fallback is a text image label.
    string namedImageSource(const string& aImageSpec);

    /// @param aElementPath dot separated absolute path beginning at root container, or dot-prefixed relative path
    ///   (.elem = one of my subelements, ..elem=a sibling (element in my parent's container), ...=grandparent, etc.)
    /// @param aOrigin the origin for relative paths
    /// @return requested element or NULL if none found
    LVGLUiElementPtr namedElement(string aElementPath, LVGLUiElementPtr aOrigin);

    /// queue event script to run (when others scripts are done)
    void queueEventScript(lv_event_t aEvent, LVGLUiElementPtr aElement, const string &aScriptCode);

    /// queue global script (executed as refresh event for the LvGLUi container)
    void queueGlobalScript(const string &aScriptCode);

    /// load named screen and call its onrefreshscript
    void loadScreen(const string aScreenName);


  private:

    void runNextScript();
    void scriptDone();

  };



} // namespace p44






#endif // ENABLE_LVGL

#endif /* __p44utils__uielements__ */
