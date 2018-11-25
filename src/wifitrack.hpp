//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of lethd/hermeld
//
//  lethd/hermeld is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  lethd/hermeld is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with lethd/hermeld. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __lethd_wifitrack_hpp__
#define __lethd_wifitrack_hpp__

#include "feature.hpp"
#include "view.hpp"

#include <math.h>

namespace p44 {

  class WTMac;
  typedef boost::intrusive_ptr<WTMac> WTMacPtr;
  class WTSSid;
  typedef boost::intrusive_ptr<WTSSid> WTSSidPtr;
  class WTPerson;
  typedef boost::intrusive_ptr<WTPerson> WTPersonPtr;

  typedef std::map<uint64_t, WTMacPtr> WTMacMap;
  typedef std::map<string, WTSSidPtr> WTSSidMap;

  typedef std::set<WTMacPtr> WTMacSet;
  typedef std::set<WTSSidPtr> WTSSidSet;
  typedef std::set<WTPersonPtr> WTPersonSet;


  class WTMac : public P44Obj
  {
  public:

    WTMac();

    MLMicroSeconds seenLast;
    MLMicroSeconds seenFirst;
    long seenCount;
    int lastRssi;
    int bestRssi;
    int worstRssi;
    uint64_t mac;
    bool hidden;

    WTSSidSet ssids;
    WTPersonPtr person;
  };


  class WTSSid : public P44Obj
  {
  public:

    WTSSid();

    MLMicroSeconds seenLast;
    long seenCount;
    string ssid;
    bool hidden;

    int beaconRssi;
    MLMicroSeconds beaconSeenLast;

    WTMacSet macs;

  };



  class WTPerson : public P44Obj
  {
  public:

    WTPerson();

    MLMicroSeconds seenLast;
    MLMicroSeconds seenFirst;
    long seenCount;
    int lastRssi;
    int bestRssi;
    int worstRssi;

    PixelColor color;
    int imageIndex;
    string name;
    bool hidden;

    MLMicroSeconds shownLast;

    WTMacSet macs;

  };




  class WifiTrack : public Feature
  {
    typedef Feature inherited;

    string monitorIf;
    int dumpPid;
    FdCommPtr dumpStream;

    MLTicket restartTicket;
    ScriptContextPtr scriptContext;

    WTMacMap macs;
    WTSSidMap ssids;
    WTPersonSet persons;

    // settings
    bool rememberWithoutSsid;
    MLMicroSeconds minShowInterval;
    int minRssi;
    int tooCommonMacCount;
    int minCommonSsidCount;
    int numPersonImages;
    string personImagePrefix;

  public:

    WifiTrack(const string aMonitorIf);
    virtual ~WifiTrack();

    /// initialize the feature
    /// @param aInitData the init data object specifying feature init details
    /// @return error if any, NULL if ok
    virtual ErrorPtr initialize(JsonObjectPtr aInitData) override;

    /// handle request
    /// @param aRequest the API request to process
    /// @return NULL to send nothing at return (but possibly later via aRequest->sendResponse),
    ///   Error::ok() to just send a empty response, or error to report back
    virtual ErrorPtr processRequest(ApiRequestPtr aRequest) override;

    /// @return status information object for initialized feature, bool false for uninitialized
    virtual JsonObjectPtr status() override;

  private:

    void initOperation();

    ErrorPtr save();
    ErrorPtr load();

    JsonObjectPtr dataDump();
    ErrorPtr dataImport(JsonObjectPtr aData);

    void dumpEnded(ErrorPtr aError);
    void gotDumpLine(ErrorPtr aError);

    void processSighting(WTMacPtr aMac, WTSSidPtr aSSid, bool aNewSSidForMac);

    void displayMessage(int aImageIndex, PixelColor aColor, string aMessage);

  };

} // namespace p44



#endif /* __lethd_wifitrack_hpp__ */
