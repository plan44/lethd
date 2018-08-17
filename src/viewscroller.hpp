//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __pixelboardd_viewscroller_hpp__
#define __pixelboardd_viewscroller_hpp__

#include "view.hpp"

namespace p44 {

  /// Smooth scrolling (subpixel resolution)
  class ViewScroller : public View
  {
    typedef View inherited;

  private:

    ViewPtr scrolledView;

    // current scroll offsets
    long scrollOffsetX_milli; ///< in millipixel, X distance from this view's content origin to the scrolled view's origin
    long scrollOffsetY_milli; ///< in millipixel, Y distance from this view's content origin to the scrolled view's origin

    // scroll animation
    long scrollStepX_milli; ///< in millipixel, X distance to scroll per scrollStepInterval
    long scrollStepY_milli; ///< in millipixel, Y distance to scroll per scrollStepInterval
    long scrollSteps; ///< >0: number of remaining scroll steps, <0 = scroll forever, 0=scroll stopped
    MLMicroSeconds scrollStepInterval; ///< interval between scroll steps
    MLMicroSeconds nextScrollStepAt; ///< exact time when next step should occur
    SimpleCB scrollCompletedCB; ///< called when one scroll is done

  protected:

    /// get content pixel color
    /// @param aX content X coordinate
    /// @param aY content Y coordinate
    /// @note aX and aY are NOT guaranteed to be within actual content as defined by contentSizeX/Y
    ///   implementation must check this!
    virtual PixelColor contentColorAt(int aX, int aY) P44_OVERRIDE;

  public :

    /// create view
    ViewScroller();

    virtual ~ViewScroller();

    /// set the to be scrolled view
    /// @param aScrolledView the view of which a part should be shown in this view.
    void setScrolledView(ViewPtr aScrolledView) { scrolledView = aScrolledView; makeDirty(); }

    /// set scroll offsets
    /// @param aOffsetX X direction scroll offset, subpixel distances allowed
    /// @note the scroll offset describes the distance from this view's content origin (not its origin on the parent view!)
    ///   to the scrolled view's origin (not its content origin)
    void setOffsetX(double aOffsetX) { scrollOffsetX_milli = aOffsetX*1000l; makeDirty(); }

    /// set scroll offsets
    /// @param aOffsetY Y direction scroll offset, subpixel distances allowed
    /// @note the scroll offset describes the distance from this view's content origin (not its origin on the parent view!)
    ///   to the scrolled view's origin (not its content origin)
    void setOffsetY(double aOffsetY) { scrollOffsetY_milli = aOffsetY*1000l; makeDirty(); }

    /// @return the current X scroll offset
    double getOffsetX() const { return (double)scrollOffsetX_milli/1000; };

    /// @return the current Y scroll offset
    double getOffsetY() const { return (double)scrollOffsetY_milli/1000; };

    /// start scrolling
    /// @param aStepX scroll step in X direction
    /// @param aStepY scroll step in Y direction
    /// @param aInterval interval between scrolling steps
    /// @param aNumSteps number of scroll steps, <0 = forever (until stopScroll() is called)
    /// @param aRoundOffsets if set (default), current scroll offsets will be rouned to next aStepX,Y boundary first
    /// @param aStartTime time of first step in MainLoop::now() timescale. If ==Never, then now() is used
    /// @param aCompletedCB called when scroll ends because aNumSteps have been executed (but not when aborted via stopScroll())
    /// @note MainLoop::now() time is monotonic (CLOCK_MONOTONIC under Linux, but is adjusted by adjtime() from NTP
    ///   so it should remain in sync over multiple devices as long as these have NTP synced time.
    /// @note MainLoop::now() time is not absolute, but has a unspecified starting point.
    ///   and MainLoop::unixTimeToMainLoopTime() to convert a absolute starting point into now() time.
    void startScroll(double aStepX, double aStepY, MLMicroSeconds aInterval, bool aRoundOffsets = true, long aNumSteps = -1, MLMicroSeconds aStartTime = Never, SimpleCB aCompletedCB = NULL);

    /// stop scrolling
    /// @note: completed callback will not be called
    void stopScroll();

    /// @return the current X scroll step
    double getStepX() const { return (double)scrollStepX_milli/1000; }

    /// @return the current Y scroll step
    double getStepY() const { return (double)scrollStepY_milli/1000; }

    /// @return the time interval between two scroll steps
    MLMicroSeconds getScrollStepInterval() const { return scrollStepInterval; }

    /// clear contents of this view
    virtual void clear() P44_OVERRIDE;

    /// calculate changes on the display, return time of next change
    /// @return Infinite if there is no immediate need to call step again, otherwise mainloop time of when to call again latest
    /// @note this must be called as demanded by return value, and after making changes to the view
    virtual MLMicroSeconds step() P44_OVERRIDE;

    /// return if anything changed on the display since last call
    virtual bool isDirty() P44_OVERRIDE;

    /// call when display is updated
    virtual void updated() P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<ViewScroller> ViewScrollerPtr;

} // namespace p44



#endif /* __pixelboardd_viewscroller_hpp__ */
