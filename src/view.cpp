//
//  Copyright (c) 2016-2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of pixelboardd.
//
//  pixelboardd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  pixelboardd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with pixelboardd. If not, see <http://www.gnu.org/licenses/>.
//

#include "view.hpp"
#include "ledchaincomm.hpp" // for brightnessToPwm and pwmToBrightness

using namespace p44;

// MARK: ===== View

View::View()
{
  setFrame(0, 0, 0, 0);
  // default to normal orientation
  contentOrientation = right;
  // default to no content wrap
  contentWrapMode = noWrap;
  // default content size is same as view's
  offsetX = 0;
  offsetY = 0;
  contentSizeX = 0;
  contentSizeY = 0;
  backgroundColor = { .r=0, .g=0, .b=0, .a=0 }; // transparent background,
  foregroundColor = { .r=255, .g=255, .b=255, .a=255 }; // fully white foreground...
  alpha = 255; // but content pixels passed trough 1:1
  contentIsMask = false; // content color will be used
  targetAlpha = -1; // not fading
  localTimingPriority = true;
  maskChildDirtyUntil = Never;
}


View::~View()
{
  clear();
}


bool View::isInContentSize(int aX, int aY)
{
  return aX>=0 && aY>=0 && aX<contentSizeX && aY<contentSizeY;
}


void View::setFrame(int aOriginX, int aOriginY, int aSizeX, int aSizeY)
{
  originX = aOriginX;
  originY = aOriginY;
  dX = aSizeX,
  dY = aSizeY;
  makeDirty();
}


void View::clear()
{
  setContentSize(0, 0);
}


bool View::reportDirtyChilds()
{
  if (maskChildDirtyUntil) {
    if (MainLoop::now()<maskChildDirtyUntil) {
      return false;
    }
    maskChildDirtyUntil = 0;
  }
  return true;
}


void View::updateNextCall(MLMicroSeconds &aNextCall, MLMicroSeconds aCallCandidate, MLMicroSeconds aCandidatePriorityUntil)
{
  if (localTimingPriority && aCandidatePriorityUntil>0 && aCallCandidate>=0 && aCallCandidate<aCandidatePriorityUntil) {
    // children must not cause "dirty" before candidate time is over
    MLMicroSeconds now = MainLoop::now();
    maskChildDirtyUntil = (aCallCandidate-now)*2+now; // duplicate to make sure candidate execution has some time to happen BEFORE dirty is unblocked
  }
  if (aNextCall<=0 || (aCallCandidate>0 && aCallCandidate<aNextCall)) {
    // candidate wins
    aNextCall = aCallCandidate;
  }
}


MLMicroSeconds View::step(MLMicroSeconds aPriorityUntil)
{
  // check fading
  if (targetAlpha>=0) {
    MLMicroSeconds now = MainLoop::now();
    double timeDone = (double)(now-startTime)/fadeTime;
    if (timeDone<1) {
      // continue fading
      int currentAlpha = targetAlpha-(1-timeDone)*fadeDist;
      setAlpha(currentAlpha);
      // return recommended call-again-time for smooth fading
      return now+fadeTime/fadeDist; // single alpha steps
    }
    else {
      // target alpha reached
      setAlpha(targetAlpha);
      targetAlpha = -1;
      // call back
      if (fadeCompleteCB) {
        SimpleCB cb = fadeCompleteCB;
        fadeCompleteCB = NULL;
        cb();
      }
    }
  }
  return Infinite; // completed
}


void View::setAlpha(int aAlpha)
{
  if (alpha!=aAlpha) {
    alpha = aAlpha;
    makeDirty();
  }
}


void View::stopFading()
{
  targetAlpha = -1;
  fadeCompleteCB = NULL; // did not run to end
}


void View::fadeTo(int aAlpha, MLMicroSeconds aWithIn, SimpleCB aCompletedCB)
{
  fadeDist = aAlpha-alpha;
  startTime = MainLoop::now();
  fadeTime = aWithIn;
  if (fadeTime<=0 || fadeDist==0) {
    // immediate
    setAlpha(aAlpha);
    targetAlpha = -1;
    if (aCompletedCB) aCompletedCB();
  }
  else {
    // start fading
    targetAlpha = aAlpha;
    fadeCompleteCB = aCompletedCB;
  }
}


void View::setFullFrameContent()
{
  setContentSize(dX, dY);
  setContentOffset(0, 0);
  setOrientation(View::right);
}


void View::sizeFrameToContent()
{
  int csx = contentSizeX;
  int csy = contentSizeY;
  if (contentOrientation & xy_swap) {
    swap(csx, csy);
  }
  dX = offsetX+csx;
  dY = offsetY+csy;
  makeDirty();
}




#define SHOW_ORIGIN 0

PixelColor View::colorAt(int aX, int aY)
{
  // default is background color
  PixelColor pc = backgroundColor;
  if (alpha==0) {
    pc.a = 0; // entire view is invisible
  }
  else {
    // calculate coordinate relative to the content's origin
    int x = aX-originX-offsetX;
    int y = aY-originY-offsetY;
    // translate into content coordinates
    if (contentOrientation & xy_swap) {
      swap(x, y);
    }
    if (contentOrientation & x_flip) {
      x = contentSizeX-x-1;
    }
    if (contentOrientation & y_flip) {
      y = contentSizeY-y-1;
    }
    // optionally clip content
    if (contentWrapMode&clipXY && (
      ((contentWrapMode&clipXmin) && x<0) ||
      ((contentWrapMode&clipXmax) && x>=contentSizeX) ||
      ((contentWrapMode&clipYmin) && y<0) ||
      ((contentWrapMode&clipYmax) && y>=contentSizeY)
    )) {
      // clip
      pc.a = 0; // invisible
    }
    else {
      // not clipped
      // optionally wrap content
      if (contentSizeX>0) {
        while ((contentWrapMode&wrapXmin) && x<0) x+=contentSizeX;
        while ((contentWrapMode&wrapXmax) && x>=contentSizeX) x-=contentSizeX;
      }
      if (contentSizeY>0) {
        while ((contentWrapMode&wrapYmin) && y<0) y+=contentSizeY;
        while ((contentWrapMode&wrapYmax) && y>=contentSizeY) y-=contentSizeY;
      }
      // now get content pixel
      pc = contentColorAt(x, y);
      if (contentIsMask) {
        // use only alpha of content, color comes from foregroundColor
        pc.r = foregroundColor.r;
        pc.g = foregroundColor.g;
        pc.b = foregroundColor.b;
      }
      #if SHOW_ORIGIN
      if (x==0 && y==0) {
        return { .r=255, .g=0, .b=0, .a=255 };
      }
      else if (x==1 && y==0) {
        return { .r=0, .g=255, .b=0, .a=255 };
      }
      else if (x==0 && y==1) {
        return { .r=0, .g=0, .b=255, .a=255 };
      }
      #endif
      if (pc.a==0) {
        // background is where content is fully transparent
        pc = backgroundColor;
        // Note: view background does NOT shine through semi-transparent content pixels!
        //   But non-transparent content pixels directly are view pixels!
      }
      // factor in layer alpha
      if (alpha!=255) {
        pc.a = dimVal(pc.a, alpha);
      }
    }
  }
  return pc;
}


// MARK: ===== Utilities

uint8_t p44::dimVal(uint8_t aVal, uint16_t aDim)
{
  uint32_t d = (aDim+1)*aVal;
  if (d>0xFFFF) return 0xFF;
  return d>>8;
}


void p44::dimPixel(PixelColor &aPix, uint16_t aDim)
{
  aPix.r = dimVal(aPix.r, aDim);
  aPix.g = dimVal(aPix.g, aDim);
  aPix.b = dimVal(aPix.b, aDim);
}


PixelColor p44::dimmedPixel(const PixelColor aPix, uint16_t aDim)
{
  PixelColor pix = aPix;
  dimPixel(pix, aDim);
  return pix;
}


void p44::alpahDimPixel(PixelColor &aPix)
{
  if (aPix.a!=255) {
    dimPixel(aPix, aPix.a);
  }
}


void p44::reduce(uint8_t &aByte, uint8_t aAmount, uint8_t aMin)
{
  int r = aByte-aAmount;
  if (r<aMin)
    aByte = aMin;
  else
    aByte = (uint8_t)r;
}


void p44::increase(uint8_t &aByte, uint8_t aAmount, uint8_t aMax)
{
  int r = aByte+aAmount;
  if (r>aMax)
    aByte = aMax;
  else
    aByte = (uint8_t)r;
}


void p44::overlayPixel(PixelColor &aPixel, PixelColor aOverlay)
{
  if (aOverlay.a==255) {
    aPixel = aOverlay;
  }
  else {
    // mix in
    // - reduce original by alpha of overlay
    aPixel = dimmedPixel(aPixel, 255-aOverlay.a);
    // - reduce overlay by its own alpha
    aOverlay = dimmedPixel(aOverlay, aOverlay.a);
    // - add in
    addToPixel(aPixel, aOverlay);
  }
  aPixel.a = 255; // result is never transparent
}


void p44::mixinPixel(PixelColor &aMainPixel, PixelColor aOutsidePixel, uint8_t aAmountOutside)
{
  if (aAmountOutside>0) {
    if (aMainPixel.a!=255 || aOutsidePixel.a!=255) {
      // mixed transparency
      uint8_t alpha = dimVal(aMainPixel.a, pwmToBrightness(255-aAmountOutside)) + dimVal(aOutsidePixel.a, pwmToBrightness(aAmountOutside));
      if (alpha>0) {
        // calculation only needed for non-transparent result
        // - alpha boost compensates for energy
        uint16_t ab = 65025/alpha;
        // Note: aAmountOutside is on the energy scale, not brightness, so need to add in PWM scale!
        uint16_t r_e = ( (((uint16_t)brightnessToPwm(dimVal(aMainPixel.r, aMainPixel.a))+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(dimVal(aOutsidePixel.r, aOutsidePixel.a))+1)*(aAmountOutside)) )>>8;
        uint16_t g_e = ( (((uint16_t)brightnessToPwm(dimVal(aMainPixel.g, aMainPixel.a))+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(dimVal(aOutsidePixel.g, aOutsidePixel.a))+1)*(aAmountOutside)) )>>8;
        uint16_t b_e = ( (((uint16_t)brightnessToPwm(dimVal(aMainPixel.b, aMainPixel.a))+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(dimVal(aOutsidePixel.b, aOutsidePixel.a))+1)*(aAmountOutside)) )>>8;
        // - back to brightness, add alpha boost
        uint16_t r = (((uint16_t)pwmToBrightness(r_e)+1)*ab)>>8;
        uint16_t g = (((uint16_t)pwmToBrightness(g_e)+1)*ab)>>8;
        uint16_t b = (((uint16_t)pwmToBrightness(b_e)+1)*ab)>>8;
        // - check max brightness
        uint16_t m = r; if (g>m) m = g; if (b>m) m = b;
        if (m>255) {
          // more brightness requested than we have
          // - scale down to make max=255
          uint16_t cr = 65025/m;
          r = (r*cr)>>8;
          g = (g*cr)>>8;
          b = (b*cr)>>8;
          // - increase alpha by reduction of components
          alpha = (((uint16_t)alpha+1)*m)>>8;
          aMainPixel.r = r>255 ? 255 : r;
          aMainPixel.g = g>255 ? 255 : g;
          aMainPixel.b = b>255 ? 255 : b;
          aMainPixel.a = alpha>255 ? 255 : alpha;
        }
        else {
          // brightness below max, just convert back
          aMainPixel.r = r;
          aMainPixel.g = g;
          aMainPixel.b = b;
          aMainPixel.a = alpha;
        }
      }
      else {
        // resulting alpha is 0, fully transparent pixel
        aMainPixel = transparent;
      }
    }
    else {
      // no transparency on either side, simplified case
      uint16_t r_e = ( (((uint16_t)brightnessToPwm(aMainPixel.r)+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(aOutsidePixel.r)+1)*(aAmountOutside)) )>>8;
      uint16_t g_e = ( (((uint16_t)brightnessToPwm(aMainPixel.g)+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(aOutsidePixel.g)+1)*(aAmountOutside)) )>>8;
      uint16_t b_e = ( (((uint16_t)brightnessToPwm(aMainPixel.b)+1)*(255-aAmountOutside)) + (((uint16_t)brightnessToPwm(aOutsidePixel.b)+1)*(aAmountOutside)) )>>8;
      aMainPixel.r = r_e>255 ? 255 : pwmToBrightness(r_e);
      aMainPixel.g = g_e>255 ? 255 : pwmToBrightness(g_e);
      aMainPixel.b = b_e>255 ? 255 : pwmToBrightness(b_e);
      aMainPixel.a = 255;
    }
  }
}


void p44::addToPixel(PixelColor &aPixel, PixelColor aPixelToAdd)
{
  increase(aPixel.r, aPixelToAdd.r);
  increase(aPixel.g, aPixelToAdd.g);
  increase(aPixel.b, aPixelToAdd.b);
}


PixelColor p44::webColorToPixel(const string aWebColor)
{
  PixelColor res = transparent;
  size_t i = 0;
  size_t n = aWebColor.size();
  if (n>0 && aWebColor[0]=='#') { i++; n--; } // skip optional #
  uint32_t h;
  if (sscanf(aWebColor.c_str()+i, "%x", &h)==1) {
    if (n<=4) {
      // short form RGB or ARGB
      res.a = 255;
      if (n==4) { res.a = (h>>12)&0xF; res.a |= res.a<<4; }
      res.r = (h>>8)&0xF; res.r |= res.r<<4;
      res.g = (h>>4)&0xF; res.g |= res.g<<4;
      res.b = (h>>0)&0xF; res.b |= res.b<<4;
    }
    else {
      // long form RRGGBB or AARRGGBB
      res.a = 255;
      if (n==8) { res.a = (h>>24)&0xFF; }
      res.r = (h>>16)&0xFF;
      res.g = (h>>8)&0xFF;
      res.b = (h>>0)&0xFF;
    }
  }
  return res;
}


string p44::pixelToWebColor(const PixelColor aPixelColor)
{
  string w;
  if (aPixelColor.a!=255) string_format_append(w, "%02X", aPixelColor.a);
  string_format_append(w, "%02X%02X%02X", aPixelColor.r, aPixelColor.g, aPixelColor.b);
  return w;
}


#if ENABLE_VIEWCONFIG

// MARK: ===== view configuration

ErrorPtr View::configureView(JsonObjectPtr aViewConfig)
{
  JsonObjectPtr o;
  if (aViewConfig->get("label", o)) {
    label = o->stringValue();
  }
  if (aViewConfig->get("clear", o)) {
    if(o->boolValue()) clear();
  }
  if (aViewConfig->get("x", o)) {
    originX = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("y", o)) {
    originY = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("dx", o)) {
    dX = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("dy", o)) {
    dY = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("bgcolor", o)) {
    backgroundColor = webColorToPixel(o->stringValue()); makeDirty();
  }
  if (aViewConfig->get("color", o)) {
    foregroundColor = webColorToPixel(o->stringValue()); makeDirty();
  }
  if (aViewConfig->get("alpha", o)) {
    setAlpha(o->int32Value());
  }
  if (aViewConfig->get("wrapmode", o)) {
    setWrapMode(o->int32Value());
  }
  if (aViewConfig->get("mask", o)) {
    contentIsMask = o->boolValue();
  }
  if (aViewConfig->get("content_x", o)) {
    offsetX = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("content_y", o)) {
    offsetY = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("content_dx", o)) {
    contentSizeX = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("content_dy", o)) {
    contentSizeY = o->int32Value(); makeDirty();
  }
  if (aViewConfig->get("orientation", o)) {
    setOrientation(o->int32Value());
  }
  if (aViewConfig->get("fullframe", o)) {
    if(o->boolValue()) setFullFrameContent();
  }
  if (aViewConfig->get("sizetocontent", o)) {
    if(o->boolValue()) sizeFrameToContent();
  }
  if (aViewConfig->get("timingpriority", o)) {
    localTimingPriority = o->boolValue();
  }
  return ErrorPtr();
}


ViewPtr View::getView(const string aLabel)
{
  if (aLabel==label) {
    return ViewPtr(this); // that's me
  }
  return ViewPtr(); // not found
}



#endif // ENABLE_VIEWCONFIG



