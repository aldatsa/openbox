// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-

#ifdef HAVE_CONFIG_H
# include "../config.h"
#endif

#include "client.hh"
#include "frame.hh"
#include "bbscreen.hh"
#include "openbox.hh"
#include "otk/display.hh"
#include "otk/property.hh"

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <assert.h>

#include "gettext.h"
#define _(str) gettext(str)
}

namespace ob {

OBClient::OBClient(int screen, Window window)
  : otk::OtkEventHandler(),
    _screen(screen), _window(window)
{
  assert(screen >= 0);
  assert(window);

  Openbox::instance->registerHandler(_window, this);

  ignore_unmaps = 0;
  
  // update EVERYTHING the first time!!

  // the state is kinda assumed to be normal. is this right? XXX
  _wmstate = NormalState;
  // no default decors or functions, each has to be enabled
  _decorations = _functions = 0;
  
  getArea();
  getDesktop();
  getType();

  // set the decorations and functions
  switch (_type) {
  case Type_Normal:
    // normal windows retain all of the possible decorations and
    // functionality
    _decorations = Decor_Titlebar | Decor_Handle | Decor_Border |
                   Decor_Iconify | Decor_Maximize;
    _functions = Func_Resize | Func_Move | Func_Iconify | Func_Maximize;

  case Type_Dialog:
    // dialogs cannot be maximized
    _decorations &= ~Decor_Maximize;
    _functions &= ~Func_Maximize;
    break;

  case Type_Menu:
  case Type_Toolbar:
  case Type_Utility:
    // these windows get less functionality
    _decorations &= ~(Decor_Iconify | Decor_Handle);
    _functions &= ~(Func_Iconify | Func_Resize);
    break;

  case Type_Desktop:
  case Type_Dock:
  case Type_Splash:
    // none of these windows are manipulated by the window manager
    _decorations = 0;
    _functions = 0;
    break;
  }
  
  getMwmHints(); // this fucks (in good ways) with the decors and functions
  getState();
  getShaped();

  updateProtocols();
  updateNormalHints();
  updateWMHints();
  // XXX: updateTransientFor();
  updateTitle();
  updateIconTitle();
  updateClass();

/*
#ifdef DEBUG
  printf("Mapped window: 0x%lx\n"
         "  title:         \t%s\t  icon title:    \t%s\n"
         "  app name:      \t%s\t\t  class:         \t%s\n"
         "  position:      \t%d, %d\t\t  size:          \t%d, %d\n"
         "  desktop:       \t%lu\t\t  group:         \t0x%lx\n"
         "  type:          \t%d\t\t  min size       \t%d, %d\n"
         "  base size      \t%d, %d\t\t  max size       \t%d, %d\n"
         "  size incr      \t%d, %d\t\t  gravity        \t%d\n"
         "  wm state       \t%ld\t\t  can be focused:\t%s\n"
         "  notify focus:  \t%s\t\t  urgent:        \t%s\n"
         "  shaped:        \t%s\t\t  modal:         \t%s\n"
         "  shaded:        \t%s\t\t  iconic:        \t%s\n"
         "  vert maximized:\t%s\t\t  horz maximized:\t%s\n"
         "  fullscreen:    \t%s\t\t  floating:      \t%s\n"
         "  requested pos: \t%s\n",
         _window,
         _title.c_str(),
         _icon_title.c_str(),
         _app_name.c_str(),
         _app_class.c_str(),
         _area.x(), _area.y(),
         _area.width(), _area.height(),
         _desktop,
         _group,
         _type,
         _min_x, _min_y,
         _base_x, _base_y,
         _max_x, _max_y,
         _inc_x, _inc_y,
         _gravity,
         _wmstate,
         _can_focus ? "yes" : "no",
         _focus_notify ? "yes" : "no",
         _urgent ? "yes" : "no",
         _shaped ? "yes" : "no",
         _modal ? "yes" : "no",
         _shaded ? "yes" : "no",
         _iconic ? "yes" : "no",
         _max_vert ? "yes" : "no",
         _max_horz ? "yes" : "no",
         _fullscreen ? "yes" : "no",
         _floating ? "yes" : "no",
         _positioned ? "yes" : "no");
#endif
*/
}


OBClient::~OBClient()
{
  const otk::OBProperty *property = Openbox::instance->property();

  // these values should not be persisted across a window unmapping/mapping
  property->erase(_window, otk::OBProperty::net_wm_desktop);
  property->erase(_window, otk::OBProperty::net_wm_state);
}


void OBClient::getDesktop()
{
  const otk::OBProperty *property = Openbox::instance->property();

  // defaults to the current desktop
  _desktop = 0; // XXX: change this to the current desktop!

  property->get(_window, otk::OBProperty::net_wm_desktop,
                otk::OBProperty::Atom_Cardinal,
                &_desktop);
}


void OBClient::getType()
{
  const otk::OBProperty *property = Openbox::instance->property();

  _type = (WindowType) -1;
  
  unsigned long *val;
  unsigned long num = (unsigned) -1;
  if (property->get(_window, otk::OBProperty::net_wm_window_type,
                    otk::OBProperty::Atom_Atom,
                    &num, &val)) {
    // use the first value that we know about in the array
    for (unsigned long i = 0; i < num; ++i) {
      if (val[i] ==
          property->atom(otk::OBProperty::net_wm_window_type_desktop))
        _type = Type_Desktop;
      else if (val[i] ==
               property->atom(otk::OBProperty::net_wm_window_type_dock))
        _type = Type_Dock;
      else if (val[i] ==
               property->atom(otk::OBProperty::net_wm_window_type_toolbar))
        _type = Type_Toolbar;
      else if (val[i] ==
               property->atom(otk::OBProperty::net_wm_window_type_menu))
        _type = Type_Menu;
      else if (val[i] ==
               property->atom(otk::OBProperty::net_wm_window_type_utility))
        _type = Type_Utility;
      else if (val[i] ==
               property->atom(otk::OBProperty::net_wm_window_type_splash))
        _type = Type_Splash;
      else if (val[i] ==
               property->atom(otk::OBProperty::net_wm_window_type_dialog))
        _type = Type_Dialog;
      else if (val[i] ==
               property->atom(otk::OBProperty::net_wm_window_type_normal))
        _type = Type_Normal;
//      else if (val[i] ==
//               property->atom(otk::OBProperty::kde_net_wm_window_type_override))
//        mwm_decorations = 0; // prevent this window from getting any decor
      // XXX: make this work again
    }
    delete val;
  }
    
  if (_type == (WindowType) -1) {
    /*
     * the window type hint was not set, which means we either classify ourself
     * as a normal window or a dialog, depending on if we are a transient.
     */
    // XXX: make this code work!
    //if (isTransient())
    //  _type = Type_Dialog;
    //else
      _type = Type_Normal;
  }
}


void OBClient::getMwmHints()
{
  const otk::OBProperty *property = Openbox::instance->property();

  unsigned long num;
  MwmHints *hints;

  num = MwmHints::elements;
  if (!property->get(_window, otk::OBProperty::motif_wm_hints,
                     otk::OBProperty::motif_wm_hints, &num,
                     (unsigned long **)&hints))
    return;
  
  if (num < MwmHints::elements) {
    delete [] hints;
    return;
  }

  // retrieved the hints
  // Mwm Hints are applied subtractively to what has already been chosen for
  // decor and functionality

  if (hints->flags & MwmFlag_Decorations) {
    if (! (hints->decorations & MwmDecor_All)) {
      if (! (hints->decorations & MwmDecor_Border))
        _decorations &= ~Decor_Border;
      if (! (hints->decorations & MwmDecor_Handle))
        _decorations &= ~Decor_Handle;
      if (! (hints->decorations & MwmDecor_Title))
        _decorations &= ~Decor_Titlebar;
      if (! (hints->decorations & MwmDecor_Iconify))
        _decorations &= ~Decor_Iconify;
      if (! (hints->decorations & MwmDecor_Maximize))
        _decorations &= ~Decor_Maximize;
    }
  }

  if (hints->flags & MwmFlag_Functions) {
    if (! (hints->functions & MwmFunc_All)) {
      if (! (hints->functions & MwmFunc_Resize))
        _functions &= ~Func_Resize;
      if (! (hints->functions & MwmFunc_Move))
        _functions &= ~Func_Move;
      if (! (hints->functions & MwmFunc_Iconify))
        _functions &= ~Func_Iconify;
      if (! (hints->functions & MwmFunc_Maximize))
        _functions &= ~Func_Maximize;
      //if (! (hints->functions & MwmFunc_Close))
      //  _functions &= ~Func_Close;
    }
  }
  delete [] hints;
}


void OBClient::getArea()
{
  XWindowAttributes wattrib;
  Status ret;
  
  ret = XGetWindowAttributes(otk::OBDisplay::display, _window, &wattrib);
  assert(ret != BadWindow);

  _area.setRect(wattrib.x, wattrib.y, wattrib.width, wattrib.height);
  _border_width = wattrib.border_width;
}


void OBClient::getState()
{
  const otk::OBProperty *property = Openbox::instance->property();

  _modal = _shaded = _max_horz = _max_vert = _fullscreen = _floating = false;
  
  unsigned long *state;
  unsigned long num = (unsigned) -1;
  
  if (property->get(_window, otk::OBProperty::net_wm_state,
                    otk::OBProperty::Atom_Atom, &num, &state)) {
    for (unsigned long i = 0; i < num; ++i) {
      if (state[i] == property->atom(otk::OBProperty::net_wm_state_modal))
        _modal = true;
      else if (state[i] ==
               property->atom(otk::OBProperty::net_wm_state_shaded))
        _shaded = true;
      else if (state[i] ==
               property->atom(otk::OBProperty::net_wm_state_fullscreen))
        _fullscreen = true;
      else if (state[i] ==
               property->atom(otk::OBProperty::net_wm_state_maximized_vert))
        _max_vert = true;
      else if (state[i] ==
               property->atom(otk::OBProperty::net_wm_state_maximized_horz))
        _max_horz = true;
    }

    delete [] state;
  }
}


void OBClient::getShaped()
{
  _shaped = false;
#ifdef   SHAPE
  if (otk::OBDisplay::shape()) {
    int foo;
    unsigned int ufoo;
    int s;

    XShapeSelectInput(otk::OBDisplay::display, _window, ShapeNotifyMask);

    XShapeQueryExtents(otk::OBDisplay::display, _window, &s, &foo,
                       &foo, &ufoo, &ufoo, &foo, &foo, &foo, &ufoo, &ufoo);
    _shaped = (s != 0);
  }
#endif // SHAPE
}


void OBClient::updateProtocols()
{
  const otk::OBProperty *property = Openbox::instance->property();

  Atom *proto;
  int num_return = 0;

  _focus_notify = false;
  _decorations &= ~Decor_Close;
  _functions &= ~Func_Close;

  if (XGetWMProtocols(otk::OBDisplay::display, _window, &proto, &num_return)) {
    for (int i = 0; i < num_return; ++i) {
      if (proto[i] == property->atom(otk::OBProperty::wm_delete_window)) {
        _decorations |= Decor_Close;
        _functions |= Func_Close;
        // XXX: update the decor?
      } else if (proto[i] == property->atom(otk::OBProperty::wm_take_focus))
        // if this protocol is requested, then the window will be notified
        // by the window manager whenever it receives focus
        _focus_notify = true;
    }
    XFree(proto);
  }
}


void OBClient::updateNormalHints()
{
  XSizeHints size;
  long ret;

  // defaults
  _gravity = NorthWestGravity;
  _size_inc.setPoint(1, 1);
  _base_size.setPoint(0, 0);
  _min_size.setPoint(0, 0);
  _max_size.setPoint(INT_MAX, INT_MAX);

  // XXX: might want to cancel any interactive resizing of the window at this
  // point..

  // get the hints from the window
  if (XGetWMNormalHints(otk::OBDisplay::display, _window, &size, &ret)) {
    _positioned = (size.flags & (PPosition|USPosition));

    if (size.flags & PWinGravity)
      _gravity = size.win_gravity;
    
    if (size.flags & PMinSize)
      _min_size.setPoint(size.min_width, size.min_height);
    
    if (size.flags & PMaxSize)
      _max_size.setPoint(size.max_width, size.max_height);
    
    if (size.flags & PBaseSize)
      _base_size.setPoint(size.base_width, size.base_height);
    
    if (size.flags & PResizeInc)
      _size_inc.setPoint(size.width_inc, size.height_inc);
  }
}


void OBClient::updateWMHints()
{
  XWMHints *hints;

  // assume a window takes input if it doesnt specify
  _can_focus = true;
  _urgent = false;
  
  if ((hints = XGetWMHints(otk::OBDisplay::display, _window)) != NULL) {
    if (hints->flags & InputHint)
      _can_focus = hints->input;

    if (hints->flags & XUrgencyHint)
      _urgent = true;

    if (hints->flags & WindowGroupHint) {
      if (hints->window_group != _group) {
        // XXX: remove from the old group if there was one
        _group = hints->window_group;
        // XXX: do stuff with the group
      }
    } else // no group!
      _group = None;

    XFree(hints);
  }
}


void OBClient::updateTitle()
{
  const otk::OBProperty *property = Openbox::instance->property();

  _title = "";
  
  // try netwm
  if (! property->get(_window, otk::OBProperty::net_wm_name,
                      otk::OBProperty::utf8, &_title)) {
    // try old x stuff
    property->get(_window, otk::OBProperty::wm_name,
                  otk::OBProperty::ascii, &_title);
  }

  if (_title.empty())
    _title = _("Unnamed Window");
}


void OBClient::updateIconTitle()
{
  const otk::OBProperty *property = Openbox::instance->property();

  _icon_title = "";
  
  // try netwm
  if (! property->get(_window, otk::OBProperty::net_wm_icon_name,
                      otk::OBProperty::utf8, &_icon_title)) {
    // try old x stuff
    property->get(_window, otk::OBProperty::wm_icon_name,
                  otk::OBProperty::ascii, &_icon_title);
  }

  if (_title.empty())
    _icon_title = _("Unnamed Window");
}


void OBClient::updateClass()
{
  const otk::OBProperty *property = Openbox::instance->property();

  // set the defaults
  _app_name = _app_class = "";

  otk::OBProperty::StringVect v;
  unsigned long num = 2;

  if (! property->get(_window, otk::OBProperty::wm_class,
                      otk::OBProperty::ascii, &num, &v))
    return;

  if (num > 0) _app_name = v[0];
  if (num > 1) _app_class = v[1];
}


void OBClient::propertyHandler(const XPropertyEvent &e)
{
  otk::OtkEventHandler::propertyHandler(e);
  
  const otk::OBProperty *property = Openbox::instance->property();

  // compress changes to a single property into a single change
  XEvent ce;
  while (XCheckTypedEvent(otk::OBDisplay::display, e.type, &ce)) {
    // XXX: it would be nice to compress ALL changes to a property, not just
    //      changes in a row without other props between.
    if (ce.xproperty.atom != e.atom) {
      XPutBackEvent(otk::OBDisplay::display, &ce);
      break;
    }
  }

  if (e.atom == XA_WM_NORMAL_HINTS)
    updateNormalHints();
  else if (e.atom == XA_WM_HINTS)
    updateWMHints();
  else if (e.atom == property->atom(otk::OBProperty::net_wm_name) ||
           e.atom == property->atom(otk::OBProperty::wm_name))
    updateTitle();
  else if (e.atom == property->atom(otk::OBProperty::net_wm_icon_name) ||
           e.atom == property->atom(otk::OBProperty::wm_icon_name))
    updateIconTitle();
  else if (e.atom == property->atom(otk::OBProperty::wm_class))
    updateClass();
  else if (e.atom == property->atom(otk::OBProperty::wm_protocols))
    updateProtocols();
  // XXX: transient for hint
  // XXX: strut hint
}


void OBClient::setWMState(long state)
{
  if (state == _wmstate) return; // no change
  
  switch (state) {
  case IconicState:
    // XXX: cause it to iconify
    break;
  case NormalState:
    // XXX: cause it to uniconify
    break;
  }
  _wmstate = state;
}


void OBClient::setDesktop(long target)
{
  assert(target >= 0);
  //assert(target == 0xffffffff || target < MAX);
  
  // XXX: move the window to the new desktop
  _desktop = target;
}


void OBClient::setState(StateAction action, long data1, long data2)
{
  const otk::OBProperty *property = Openbox::instance->property();

  if (!(action == State_Add || action == State_Remove ||
        action == State_Toggle))
    return; // an invalid action was passed to the client message, ignore it

  for (int i = 0; i < 2; ++i) {
    Atom state = i == 0 ? data1 : data2;
    
    if (! state) continue;

    // if toggling, then pick whether we're adding or removing
    if (action == State_Toggle) {
      if (state == property->atom(otk::OBProperty::net_wm_state_modal))
        action = _modal ? State_Remove : State_Add;
      else if (state ==
               property->atom(otk::OBProperty::net_wm_state_maximized_vert))
        action = _max_vert ? State_Remove : State_Add;
      else if (state ==
               property->atom(otk::OBProperty::net_wm_state_maximized_horz))
        action = _max_horz ? State_Remove : State_Add;
      else if (state == property->atom(otk::OBProperty::net_wm_state_shaded))
        action = _shaded ? State_Remove : State_Add;
      else if (state ==
               property->atom(otk::OBProperty::net_wm_state_fullscreen))
        action = _fullscreen ? State_Remove : State_Add;
      else if (state == property->atom(otk::OBProperty::net_wm_state_floating))
        action = _floating ? State_Remove : State_Add;
    }
    
    if (action == State_Add) {
      if (state == property->atom(otk::OBProperty::net_wm_state_modal)) {
        if (_modal) continue;
        _modal = true;
        // XXX: give it focus if another window has focus that shouldnt now
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_maximized_vert)){
        if (_max_vert) continue;
        _max_vert = true;
        // XXX: resize the window etc
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_maximized_horz)){
        if (_max_horz) continue;
        _max_horz = true;
        // XXX: resize the window etc
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_shaded)) {
        if (_shaded) continue;
        _shaded = true;
        // XXX: hide the client window
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_fullscreen)) {
        if (_fullscreen) continue;
        _fullscreen = true;
        // XXX: raise the window n shit
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_floating)) {
        if (_floating) continue;
        _floating = true;
        // XXX: raise the window n shit
      }

    } else { // action == State_Remove
      if (state == property->atom(otk::OBProperty::net_wm_state_modal)) {
        if (!_modal) continue;
        _modal = false;
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_maximized_vert)){
        if (!_max_vert) continue;
        _max_vert = false;
        // XXX: resize the window etc
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_maximized_horz)){
        if (!_max_horz) continue;
        _max_horz = false;
        // XXX: resize the window etc
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_shaded)) {
        if (!_shaded) continue;
        _shaded = false;
        // XXX: show the client window
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_fullscreen)) {
        if (!_fullscreen) continue;
        _fullscreen = false;
        // XXX: lower the window to its proper layer
      } else if (state ==
                 property->atom(otk::OBProperty::net_wm_state_floating)) {
        if (!_floating) continue;
        _floating = false;
        // XXX: lower the window to its proper layer
      }
    }
  }
}


void OBClient::clientMessageHandler(const XClientMessageEvent &e)
{
  otk::OtkEventHandler::clientMessageHandler(e);
  
  if (e.format != 32) return;

  const otk::OBProperty *property = Openbox::instance->property();
  
  if (e.message_type == property->atom(otk::OBProperty::wm_change_state)) {
    // compress changes into a single change
    bool compress = false;
    XEvent ce;
    while (XCheckTypedEvent(otk::OBDisplay::display, e.type, &ce)) {
      // XXX: it would be nice to compress ALL messages of a type, not just
      //      messages in a row without other message types between.
      if (ce.xclient.message_type != e.message_type) {
        XPutBackEvent(otk::OBDisplay::display, &ce);
        break;
      }
      compress = true;
    }
    if (compress)
      setWMState(ce.xclient.data.l[0]); // use the found event
    else
      setWMState(e.data.l[0]); // use the original event
  } else if (e.message_type ==
             property->atom(otk::OBProperty::net_wm_desktop)) {
    // compress changes into a single change 
    bool compress = false;
    XEvent ce;
    while (XCheckTypedEvent(otk::OBDisplay::display, e.type, &ce)) {
      // XXX: it would be nice to compress ALL messages of a type, not just
      //      messages in a row without other message types between.
      if (ce.xclient.message_type != e.message_type) {
        XPutBackEvent(otk::OBDisplay::display, &ce);
        break;
      }
      compress = true;
    }
    if (compress)
      setDesktop(e.data.l[0]); // use the found event
    else
      setDesktop(e.data.l[0]); // use the original event
  }
  else if (e.message_type == property->atom(otk::OBProperty::net_wm_state))
    // can't compress these
    setState((StateAction)e.data.l[0], e.data.l[1], e.data.l[2]);
}


#if defined(SHAPE) || defined(DOXYGEN_IGNORE)
void OBClient::shapeHandler(const XShapeEvent &e)
{
  otk::OtkEventHandler::shapeHandler(e);
  
  _shaped = e.shaped;
}
#endif


void OBClient::resize(Corner anchor, int w, int h)
{
  w -= _base_size.x(); 
  h -= _base_size.y();

  // is the window resizable? if it is not, then don't check its sizes, the
  // client can do what it wants and the user can't change it anyhow
  if (_min_size.x() <= _max_size.x() && _min_size.y() <= _max_size.y()) {
    // smaller than min size or bigger than max size?
    if (w < _min_size.x()) w = _min_size.x();
    else if (w > _max_size.x()) w = _max_size.x();
    if (h < _min_size.y()) h = _min_size.y();
    else if (h > _max_size.y()) h = _max_size.y();
  }

  // keep to the increments
  w /= _size_inc.x();
  h /= _size_inc.y();

  // store the logical size
  _logical_size.setPoint(w, h);

  w *= _size_inc.x();
  h *= _size_inc.y();

  w += _base_size.x();
  h += _base_size.y();
  
  switch (anchor) {
  case TopLeft:
    break;
  case TopRight:
    _area.setX(_area.x() - _area.width() - w);
    break;
  case BottomLeft:
    _area.setY(_area.y() - _area.height() - h);
    break;
  case BottomRight:
    _area.setX(_area.x() - _area.width() - w);
    _area.setY(_area.y() - _area.height() - h);
    break;
  }

  _area.setSize(w, h);

  // resize the frame to match
  frame->adjust();
}


void OBClient::move(int x, int y)
{
  _area.setPos(x, y);
  // move the frame to be in the requested position
  frame->applyGravity();
}


void OBClient::configureRequestHandler(const XConfigureRequestEvent &e)
{
  // XXX: if we are iconic (or shaded? (fvwm does that)) ignore the event

  if (e.value_mask & CWBorderWidth)
    _border_width = e.border_width;

    // resize, then move, as specified in the EWMH section 7.7
  if (e.value_mask & (CWWidth | CWHeight)) {
    int w = (e.value_mask & CWWidth) ? e.width : _area.width();
    int h = (e.value_mask & CWHeight) ? e.height : _area.height();

    Corner corner;
    switch (_gravity) {
    case NorthEastGravity:
    case EastGravity:
      corner = TopRight;
      break;
    case SouthWestGravity:
    case SouthGravity:
      corner = BottomLeft;
      break;
    case SouthEastGravity:
      corner = BottomRight;
      break;
    default:     // NorthWest, Static, etc
      corner = TopLeft;
    }

    resize(corner, w, h);
  }

  if (e.value_mask & (CWX | CWY)) {
    int x = (e.value_mask & CWX) ? e.x : _area.x();
    int y = (e.value_mask & CWY) ? e.y : _area.y();
    move(x, y);
  }

  if (e.value_mask & CWStackMode) {
    switch (e.detail) {
    case Below:
    case BottomIf:
      // XXX: lower the window
      break;

    case Above:
    case TopIf:
    default:
      // XXX: raise the window
      break;
    }
  }
}


}
