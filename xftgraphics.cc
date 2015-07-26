//
//  Copyright (c) 2015
//  by Mike Romberg ( mike-romberg@comcast.net )
//
//  This file may be distributed under terms of the GPL
//
#include "xftgraphics.h"
#include "log.h"

#include <iomanip>

#include <X11/Xft/Xft.h>

static std::ostream &operator<<(std::ostream &os, const XRenderColor &c);
static std::ostream &operator<<(std::ostream &os, const XftColor &c);



XftGraphics::XftGraphics(Display *dsp, Visual *v, Drawable d, bool isWindow,
  Colormap cmap, unsigned long bgPixVal)
    : _dsp(dsp), _vis(v), _d(d), _isWindow(isWindow), _cmap(cmap),
      _bgPixVal(bgPixVal), _fgPixVal(bgPixVal), _font(dsp), _draw(0),
      _fgxftc(0), _bgxftc(0) {

    setBG(bgPixVal);
    setFG("white");

    if (_isWindow) {
        logDebug << "XftDrawCreate(): "
                 << std::hex << std::showbase << _d << std::endl;
        _draw = XftDrawCreate(_dsp, _d, _vis, _cmap);
    }
    else {
        if (depth() == 1) {
            logDebug << "XftDrawCreateBitmap(): "
                     << std::hex << std::showbase << _d << std::endl;
            _draw = XftDrawCreateBitmap(_dsp, _d);
        }
        else {
            logDebug << "XftDrawCreateAlpha(): "
                     << std::hex << std::showbase << _d << std::endl;
            _draw = XftDrawCreateAlpha(_dsp, _d, depth());
        }
    }
}

XftGraphics::~XftGraphics(void) {
    if (_draw)
        XftDrawDestroy(_draw);

    if (_fgxftc) {
        XftColorFree(_dsp, _vis, _cmap, _fgxftc);
        delete _fgxftc;
    }
    if (_bgxftc) {
        XftColorFree(_dsp, _vis, _cmap, _bgxftc);
        delete _bgxftc;
    }
}

void XftGraphics::setFont(const std::string &name) {
    _font.setFont(name);
}

unsigned int XftGraphics::depth(void) {
    Window root;
    int x, y;
    unsigned int width, height, border, depth;

    XGetGeometry(_dsp, _d, &root, &x, &y,
      &width, &height, &border, &depth);

    return depth;
}

unsigned long XftGraphics::allocColor(const std::string &name) {
    XColor exact, closest;

    if (XAllocNamedColor(_dsp, _cmap, name.c_str(), &closest, &exact ) == 0) {
        logProblem << "allocColor() : failed to alloc : "
                   << name << std::endl;
        return WhitePixel(_dsp, DefaultScreen(_dsp));
    }

    return exact.pixel;
}

void XftGraphics::setFG(const std::string &color, unsigned short alpha) {
    setFG(allocColor(color), alpha);
}

inline static unsigned short premul(unsigned short c, unsigned short alpha) {
    unsigned int ic = c;
    ic = (ic * alpha) / 0xffff;
    return ic;
}

void XftGraphics::setFG(unsigned long pixVal, unsigned short alpha) {
    //
    // Xft (and XRender) use pre-multiplied alpha!  This is not
    // a fact that jumps out at you reading the documentation for
    // Xft (because it is not mentioned at all there).
    //
    _fgPixVal = pixVal;

    XColor def;
    def.pixel = pixVal;
    XQueryColor(_dsp, _cmap, &def);

    XRenderColor xrc;
    xrc.red = premul(def.red, alpha);
    xrc.green = premul(def.green, alpha);
    xrc.blue = premul(def.blue, alpha);
    xrc.alpha = alpha;

    if (!_fgxftc)
        _fgxftc = new XftColor();
    else
        XftColorFree(_dsp, _vis, _cmap, _fgxftc);

    XftColorAllocValue(_dsp, _vis, _cmap, &xrc, _fgxftc);
}

void XftGraphics::setBG(const std::string &color, unsigned short alpha) {
    setBG(allocColor(color), alpha);
}

void XftGraphics::setBG(unsigned long pixVal, unsigned short alpha) {
    _bgPixVal = pixVal;

    XColor def;
    def.pixel = pixVal;
    XQueryColor(_dsp, _cmap, &def);

    XRenderColor xrc;
    xrc.red = premul(def.red, alpha);
    xrc.green = premul(def.green, alpha);
    xrc.blue = premul(def.blue, alpha);
    xrc.alpha = alpha;

    logDebug << "setBG: " << "X pixVal: "
             << std::hex << std::showbase << pixVal << "\n"
             << "xcolor: " << def << "\n"
             << "xftcolor: " << xrc << std::endl;

    if (!_bgxftc)
        _bgxftc = new XftColor();
    else
        XftColorFree(_dsp, _vis, _cmap, _bgxftc);

    XftColorAllocValue(_dsp, _vis, _cmap, &xrc, _bgxftc);
}

void XftGraphics::drawString(int x, int y, const std::string &str) {
    if (str.find("LOAD") != std::string::npos) {
        logDebug << "draw: '" << str << "'\n"
                 << "fg: " << *_fgxftc << "\n"
                 << "bg: " << *_bgxftc << std::endl;
    }
    XftDrawString8(_draw, _fgxftc, _font.font(), x, y,
      (XftChar8 *)str.c_str(), str.size());
}


//----------------------------------------------------------
// For debugging
//----------------------------------------------------------
std::ostream &operator<<(std::ostream &os, const XColor &c) {
    os << std::hex << std::showbase
       << "[ "
       << "( " << std::setw(6) << c.red
       << ", " << std::setw(6) << c.green
       << ", " << std::setw(6) << c.blue
       << " ), "
       << "flags:" << (unsigned)c.flags << ","
       << "pad:" << (unsigned)c.pad << ","
       << "pixel:" << c.pixel
       << " ]";
    os << std::dec << std::noshowbase;
    return os;
}

std::ostream &operator<<(std::ostream &os, const XRenderColor &c) {
    os << std::hex << std::showbase
       << "( " << std::setw(6) << c.red
       << ", " << std::setw(6) << c.green
       << ", " << std::setw(6) << c.blue
       << ", " << std::setw(6) << c.alpha
       << " )";
    os << std::dec << std::noshowbase;
    return os;
}

std::ostream &operator<<(std::ostream &os, const XftColor &c) {
    os << std::hex << std::showbase
       << "[ " << c.color
       << ", " << c.pixel
       << " ]";
    os << std::dec << std::noshowbase;
    return os;
}
//----------------------------------------------------------
