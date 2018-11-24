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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 6


#include "wifitrack.hpp"
#include "application.hpp"

using namespace p44;



// MARK: ===== WTMac

WTMac::WTMac() :
  seenLast(Never),
  seenFirst(Never),
  seenCount(0),
  lastRssi(-9999),
  bestRssi(-9999),
  worstRssi(9999),
  hidden(false)
{
}


// MARK: ===== WTSSid

WTSSid::WTSSid() :
  seenLast(Never),
  seenCount(0),
  hidden(false),
  beaconSeenLast(Never),
  beaconRssi(-9999)
{
}


// MARK: ===== WTPerson

WTPerson::WTPerson() :
  seenLast(Never),
  seenFirst(Never),
  seenCount(0),
  lastRssi(-9999),
  bestRssi(-9999),
  worstRssi(9999),
  shownLast(Never),
  color(white),
  imageIndex(0),
  hidden(false)
{
}



// MARK: ===== WifiTrack

WifiTrack::WifiTrack(const string aMonitorIf) :
  inherited("wifitrack"),
  monitorIf(aMonitorIf),
  dumpPid(-1),
  rememberWithoutSsid(false),
  minShowInterval(3*Minute),
  minRssi(-70),
  tooCommonMacCount(20),
  minCommonSsidCount(3),
  numPersonImages(24),
  personImagePrefix("pers_")
{
  // check for commandline-triggered standalone operation
  if (CmdLineApp::sharedCmdLineApp()->getOption("wifitrack")) {
    initOperation();
  }
}


WifiTrack::~WifiTrack()
{
}

// MARK: ==== API

ErrorPtr WifiTrack::initialize(JsonObjectPtr aInitData)
{
  initOperation();
  return Error::ok();
}


ErrorPtr WifiTrack::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    string cmd = o->stringValue();
    if (cmd=="dump") {
      JsonObjectPtr ans = dataDump();
      aRequest->sendResponse(ans, ErrorPtr());
      return ErrorPtr();
    }
    else if (cmd=="save") {
      err = save();
      return err ? err : Error::ok();
    }
    else if (cmd=="load") {
      err = load();
      return err ? err : Error::ok();
    }
    else if (cmd=="hide") {
      if (data->get("ssid", o)) {
        string s = o->stringValue();
        WTSSidMap::iterator pos = ssids.find(s);
        if (pos!=ssids.end()) {
          pos->second->hidden = true;
        }
      }
      else if (data->get("mac", o)) {
        uint64_t mac = stringToMacAddress(o->stringValue().c_str());
        WTMacMap::iterator pos = macs.find(mac);
        if (pos!=macs.end()) {
          if (data->get("withperson", o)) {
            if (pos->second->person && o->boolValue()) pos->second->person->hidden = true; // hide associated person
          }
          pos->second->hidden = true;
        }
      }
      return Error::ok();
    }
    else {
      return inherited::processRequest(aRequest);
    }
  }
  else {
    // decode properties
    if (data->get("minShowInterval", o, true)) {
      minShowInterval = o->doubleValue()*MilliSecond;
    }
    if (data->get("rememberWithoutSsid", o, true)) {
      rememberWithoutSsid = o->boolValue();
    }
    if (data->get("minRssi", o, true)) {
      minRssi = o->int32Value();
    }
    if (data->get("tooCommonMacCount", o, true)) {
      tooCommonMacCount = o->int32Value();
    }
    if (data->get("minCommonSsidCount", o, true)) {
      minCommonSsidCount = o->int32Value();
    }
    if (data->get("numPersonImages", o, true)) {
      numPersonImages = o->int32Value();
    }
    if (data->get("personImagePrefix", o, true)) {
      personImagePrefix = o->stringValue();
    }
    return err ? err : Error::ok();
  }
}


JsonObjectPtr WifiTrack::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("minShowInterval", JsonObject::newDouble((double)minShowInterval/MilliSecond));
    answer->add("rememberWithoutSsid", JsonObject::newBool(rememberWithoutSsid));
    answer->add("minRssi", JsonObject::newInt32(minRssi));
    answer->add("tooCommonMacCount", JsonObject::newInt32(tooCommonMacCount));
    answer->add("minCommonSsidCount", JsonObject::newInt32(minCommonSsidCount));
    answer->add("numPersonImages", JsonObject::newInt32(numPersonImages));
    answer->add("personImagePrefix", JsonObject::newString(personImagePrefix));
  }
  return answer;
}


#define WIFITRACK_STATE_FILE_NAME "wifitrack_state.json"

ErrorPtr WifiTrack::load()
{
  JsonObjectPtr data = JsonObject::objFromFile(Application::sharedApplication()->dataPath(WIFITRACK_STATE_FILE_NAME).c_str(), NULL, 2048*1024);
  return dataImport(data);
}


ErrorPtr WifiTrack::save()
{
  JsonObjectPtr data = dataDump();
  return data->saveToFile(Application::sharedApplication()->dataPath(WIFITRACK_STATE_FILE_NAME).c_str());
}


JsonObjectPtr WifiTrack::dataDump()
{
  JsonObjectPtr ans = JsonObject::newObj();
  // persons
  JsonObjectPtr pans = JsonObject::newArray();
  for (WTPersonSet::iterator ppos = persons.begin(); ppos!=persons.end(); ++ppos) {
    JsonObjectPtr p = JsonObject::newObj();
    p->add("lastrssi", JsonObject::newInt32((*ppos)->lastRssi));
    p->add("bestrssi", JsonObject::newInt32((*ppos)->bestRssi));
    p->add("worstrssi", JsonObject::newInt32((*ppos)->worstRssi));
    if ((*ppos)->hidden) p->add("hidden", JsonObject::newBool(true));
    p->add("count", JsonObject::newInt64((*ppos)->seenCount));
    p->add("last", JsonObject::newInt64(MainLoop::mainLoopTimeToUnixTime((*ppos)->seenLast)));
    p->add("first", JsonObject::newInt64(MainLoop::mainLoopTimeToUnixTime((*ppos)->seenFirst)));
    p->add("color", JsonObject::newString(pixelToWebColor((*ppos)->color)));
    p->add("imgidx", JsonObject::newInt64((*ppos)->imageIndex));
    p->add("name", JsonObject::newString((*ppos)->name));
    JsonObjectPtr marr = JsonObject::newArray();
    for (WTMacSet::iterator mpos = (*ppos)->macs.begin(); mpos!=(*ppos)->macs.end(); ++mpos) {
      marr->arrayAppend(JsonObject::newString(macAddressToString((*mpos)->mac, ':').c_str()));
    }
    p->add("macs", marr);
    pans->arrayAppend(p);
  }
  ans->add("persons", pans);
  // macs
  JsonObjectPtr mans = JsonObject::newObj();
  for (WTMacMap::iterator mpos = macs.begin(); mpos!=macs.end(); ++mpos) {
    JsonObjectPtr m = JsonObject::newObj();
    m->add("lastrssi", JsonObject::newInt32(mpos->second->lastRssi));
    m->add("bestrssi", JsonObject::newInt32(mpos->second->bestRssi));
    m->add("worstrssi", JsonObject::newInt32(mpos->second->worstRssi));
    if (mpos->second->hidden) m->add("hidden", JsonObject::newBool(true));
    m->add("count", JsonObject::newInt64(mpos->second->seenCount));
    m->add("last", JsonObject::newInt64(MainLoop::mainLoopTimeToUnixTime(mpos->second->seenLast)));
    m->add("first", JsonObject::newInt64(MainLoop::mainLoopTimeToUnixTime(mpos->second->seenFirst)));
    JsonObjectPtr sarr = JsonObject::newArray();
    for (WTSSidSet::iterator spos = mpos->second->ssids.begin(); spos!=mpos->second->ssids.end(); ++spos) {
      sarr->arrayAppend(JsonObject::newString((*spos)->ssid));
    }
    m->add("ssids", sarr);
    mans->add(macAddressToString(mpos->first, ':').c_str(), m);
  }
  ans->add("macs", mans);
  // ssid details
  JsonObjectPtr sans = JsonObject::newObj();
  for (WTSSidMap::iterator spos = ssids.begin(); spos!=ssids.end(); ++spos) {
    JsonObjectPtr s = JsonObject::newObj();
    s->add("count", JsonObject::newInt64(spos->second->seenCount));
    s->add("last", JsonObject::newInt64(MainLoop::mainLoopTimeToUnixTime(spos->second->seenLast)));
    s->add("maccount", JsonObject::newInt64(spos->second->macs.size()));
    if (spos->second->hidden) s->add("hidden", JsonObject::newBool(true));
    if (spos->second->beaconSeenLast!=Never) {
      s->add("lastbeacon", JsonObject::newInt64(MainLoop::mainLoopTimeToUnixTime(spos->second->beaconSeenLast)));
      s->add("beaconrssi", JsonObject::newInt32(spos->second->beaconRssi));
    }
    sans->add(spos->first.c_str(), s);
  }
  ans->add("ssids", sans);
  return ans;
}


ErrorPtr WifiTrack::dataImport(JsonObjectPtr aData)
{
  if (!aData || !aData->isType(json_type_object)) return TextError::err("invalid state data - must be JSON object");
  // insert ssids
  JsonObjectPtr sobjs = aData->get("ssids");
  if (!sobjs) return TextError::err("missing 'ssids'");
  sobjs->resetKeyIteration();
  JsonObjectPtr sobj;
  string ssidstr;
  while (sobjs->nextKeyValue(ssidstr, sobj)) {
    WTSSidPtr s;
    WTSSidMap::iterator spos = ssids.find(ssidstr);
    if (spos!=ssids.end()) {
      s = spos->second;
    }
    else {
      s = WTSSidPtr(new WTSSid);
      s->ssid = ssidstr;
      ssids[ssidstr] = s;
    }
    JsonObjectPtr o;
    o = sobj->get("hidden");
    if (o) s->hidden = o->boolValue();
    o = sobj->get("count");
    if (o) s->seenCount += o->int64Value();
    o = sobj->get("last");
    MLMicroSeconds l = Never;
    if (o) l = MainLoop::unixTimeToMainLoopTime(o->int64Value());
    if (l>s->seenLast) s->seenLast = l;
  }
  // insert macs and links to ssids
  JsonObjectPtr mobjs = aData->get("macs");
  if (!mobjs) return TextError::err("missing 'macs'");
  mobjs->resetKeyIteration();
  JsonObjectPtr mobj;
  string macstr;
  while (mobjs->nextKeyValue(macstr, mobj)) {
    bool insertMac = false;
    uint64_t mac = stringToMacAddress(macstr.c_str());
    WTMacPtr m;
    WTMacMap::iterator mpos = macs.find(mac);
    if (mpos!=macs.end()) {
      m = mpos->second;
    }
    else {
      m = WTMacPtr(new WTMac);
      m->mac = mac;
      insertMac = true;
    }
    // links
    JsonObjectPtr sarr = mobj->get("ssids");
    for (int i=0; i<sarr->arrayLength(); ++i) {
      string ssidstr = sarr->arrayGet(i)->stringValue();
      if (!rememberWithoutSsid && ssidstr.empty() && sarr->arrayLength()==1) {
        insertMac = false;
      }
      WTSSidPtr s;
      WTSSidMap::iterator spos = ssids.find(ssidstr);
      if (spos!=ssids.end()) {
        s = spos->second;
      }
      else {
        s = WTSSidPtr(new WTSSid);
        s->ssid = ssidstr;
        ssids[ssidstr] = s;
      }
      m->ssids.insert(s);
      s->macs.insert(m);
    }
    if (insertMac) {
      macs[mac] = m;
    }
    // other props
    JsonObjectPtr o;
    o = mobj->get("hidden");
    if (o) m->hidden = o->boolValue();
    o = mobj->get("count");
    if (o) m->seenCount += o->int64Value();
    o = mobj->get("bestrssi");
    int r = -9999;
    if (o) r = o->int32Value();
    if (r>m->bestRssi) m->bestRssi = r;
    o = mobj->get("worstrssi");
    r = 9999;
    if (o) r = o->int32Value();
    if (r<m->worstRssi) m->worstRssi = r;
    o = mobj->get("last");
    MLMicroSeconds l = Never;
    if (o) l = MainLoop::unixTimeToMainLoopTime(o->int64Value());
    if (l>m->seenLast) {
      m->seenLast = l;
      o = mobj->get("lastrssi");
      if (o) m->lastRssi = o->int32Value();
    }
    o = mobj->get("first");
    l = Never;
    if (o) l = MainLoop::unixTimeToMainLoopTime(o->int64Value());
    if (l!=Never && m->seenFirst!=Never && l<m->seenFirst) m->seenFirst = l;
  }
  JsonObjectPtr pobjs = aData->get("persons");
  if (pobjs) {
    for (int pidx=0; pidx<pobjs->arrayLength(); pidx++) {
      JsonObjectPtr pobj = pobjs->arrayGet(pidx);
      WTPersonPtr p = WTPersonPtr(new WTPerson);
      persons.insert(p);
      // links to macs
      JsonObjectPtr marr = pobj->get("macs");
      for (int i=0; i<marr->arrayLength(); ++i) {
        string macstr = marr->arrayGet(i)->stringValue();
        uint64_t mac = stringToMacAddress(macstr.c_str());
        WTMacMap::iterator mpos = macs.find(mac);
        if (mpos!=macs.end()) {
          p->macs.insert(mpos->second);
          mpos->second->person = p;
        }
      }
      // other props
      JsonObjectPtr o;
      o = pobj->get("name");
      if (o) p->name = o->stringValue();
      o = pobj->get("color");
      if (o) p->color = webColorToPixel(o->stringValue());
      o = pobj->get("imgidx");
      if (o) p->imageIndex = o->int32Value();
      o = pobj->get("hidden");
      if (o) p->hidden = o->boolValue();
      o = pobj->get("count");
      if (o) p->seenCount += o->int64Value();
      o = pobj->get("bestrssi");
      int r = -9999;
      if (o) r = o->int32Value();
      if (r>p->bestRssi) p->bestRssi = r;
      o = pobj->get("worstrssi");
      r = 9999;
      if (o) r = o->int32Value();
      if (r<p->worstRssi) p->worstRssi = r;
      o = pobj->get("last");
      MLMicroSeconds l = Never;
      if (o) l = MainLoop::unixTimeToMainLoopTime(o->int64Value());
      if (l>p->seenLast) {
        p->seenLast = l;
        o = pobj->get("lastrssi");
        if (o) p->lastRssi = o->int32Value();
      }
      o = pobj->get("first");
      l = Never;
      if (o) l = MainLoop::unixTimeToMainLoopTime(o->int64Value());
      if (l!=Never && p->seenFirst!=Never && l<p->seenFirst) p->seenFirst = l;

    }
  }
  return ErrorPtr();
}




// MARK: ==== wifitrack operation

#define SCAN_APS 1

void WifiTrack::initOperation()
{
  LOG(LOG_NOTICE, "initializing wifitrack");

  ErrorPtr err;
  err = load();
  if (!Error::isOK(err)) {
    LOG(LOG_ERR, "could not load state: %s", Error::text(err).c_str());
  }
  #if SCAN_APS
  string cmd = string_format("tcpdump -e -i %s -s 2000 type mgt subtype probe-req or subtype beacon", monitorIf.c_str());
  #else
  string cmd = string_format("tcpdump -e -i %s -s 2000 type mgt subtype probe-req", monitorIf.c_str());
  #endif
#ifdef __APPLE__
#warning "hardcoded access to mixloop hermel"
  //cmd = "ssh -p 22 root@hermel-40a36bc18907.local. \"tcpdump -e -i moni0 -s 2000 type mgt subtype probe-req\"";
  cmd = "ssh -p 22 root@1a8479bcaf76.cust.devices.plan44.ch \"" + cmd + "\"";
#endif
  int resultFd = -1;
  dumpPid = MainLoop::currentMainLoop().fork_and_system(boost::bind(&WifiTrack::dumpEnded, this, _1), cmd.c_str(), true, &resultFd);
  if (dumpPid>=0 && resultFd>=0) {
    dumpStream = FdCommPtr(new FdComm(MainLoop::currentMainLoop()));
    dumpStream->setFd(resultFd);
    dumpStream->setReceiveHandler(boost::bind(&WifiTrack::gotDumpLine, this, _1), '\n');
  }
  // ready
  setInitialized();
}


void WifiTrack::dumpEnded(ErrorPtr aError)
{
  LOG(LOG_NOTICE, "tcpdump terminated with status: %s", Error::text(aError).c_str());
  restartTicket.executeOnce(boost::bind(&WifiTrack::initOperation, this), 5*Second);
}


void WifiTrack::gotDumpLine(ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_ERR, "error reading from tcp output stream: %s", Error::text(aError).c_str());
    return;
  }
  string line;
  if (dumpStream->receiveDelimitedString(line)) {
    LOG(LOG_DEBUG, "TCPDUMP: %s", line.c_str());
    // 17:40:22.356367 1.0 Mb/s 2412 MHz 11b -75dBm signal -75dBm signal antenna 0 -109dBm signal antenna 1 BSSID:5c:49:79:6d:28:1a (oui Unknown) DA:5c:49:79:6d:28:1a (oui Unknown) SA:c8:bc:c8:be:0d:0a (oui Unknown) Probe Request (iWay_Fiber_bu725) [1.0* 2.0* 5.5* 11.0* 6.0 9.0 12.0 18.0 Mbit]
    bool decoded = false;
    bool beacon = false;
    int rssi = 0;
    uint64_t mac = 0;
    string ssid;
    size_t s,e;
    // - rssi (signal)
    e = line.find(" signal ");
    if (e!=string::npos) {
      s = line.rfind(" ", e-1);
      if (s!=string::npos) {
        sscanf(line.c_str()+s+1, "%d", &rssi);
      }
      #if SCAN_APS
      s = line.find("Beacon (", s);
      if (s!=string::npos) {
        // Is a beacon, get SSID
        s += 8;
        e = line.find(") ", s);
        ssid = line.substr(s, e-s);
        decoded = true;
        beacon = true;
      }
      else
      #endif
      {
        // must be Probe Request
        // - sender MAC (source address)
        s = line.find("SA:");
        if (s!=string::npos) {
          mac = stringToMacAddress(line.c_str()+s+3);
          // - name of SSID probed
          s = line.find("Probe Request (", s);
          if (s!=string::npos) {
            s += 15;
            e = line.find(") ", s);
            ssid = line.substr(s, e-s);
            // - check min rssi
            if (rssi<minRssi) {
              FOCUSLOG("Too weak: RSSI=%d<%d, MAC=%s, SSID='%s'", rssi, minRssi, macAddressToString(mac,':').c_str(), ssid.c_str());
            }
            else {
              decoded = true;
            }
          }
        }
      }
    }
    if (decoded) {
      // record
      MLMicroSeconds now = MainLoop::now();
      // - SSID
      bool newSSidForMac = false;
      WTSSidPtr s;
      WTSSidMap::iterator ssidPos = ssids.find(ssid);
      if (ssidPos!=ssids.end()) {
        s = ssidPos->second;
      }
      else {
        // unknown, create
        s = WTSSidPtr(new WTSSid);
        s->ssid = ssid;
        ssids[ssid] = s;
      }
      if (beacon) {
        // just record beacon sighting
        if (s->beaconSeenLast==Never) {
          LOG(LOG_INFO, "New Beacon found: RSSI=%d, SSID='%s'", rssi, ssid.c_str());
        }
        s->beaconSeenLast = now;
        s->beaconRssi = rssi;
      }
      else {
        // process probe request
        FOCUSLOG("RSSI=%d, MAC=%s, SSID='%s'", rssi, macAddressToString(mac,':').c_str(), ssid.c_str());
        s->seenLast = now;
        s->seenCount++;
        // - MAC
        WTMacPtr m;
        WTMacMap::iterator macPos = macs.find(mac);
        if (macPos!=macs.end()) {
          m = macPos->second;
        }
        else {
          // unknown, create
          if (!s->ssid.empty() || rememberWithoutSsid) {
            m = WTMacPtr(new WTMac);
            m->mac = mac;
            macs[mac] = m;
          }
        }
        if (m) {
          m->seenCount++;
          m->seenLast = now;
          if (m->seenFirst==Never) m->seenFirst = now;
          m->lastRssi = rssi;
          if (rssi>m->bestRssi) m->bestRssi = rssi;
          if (rssi<m->worstRssi) m->worstRssi = rssi;
          // - connection
          if (m->ssids.find(s)==m->ssids.end()) {
            newSSidForMac = true;
            m->ssids.insert(s);
          }
          s->macs.insert(m);
          // process sighting
          processSighting(m, s, newSSidForMac);
        }
      }
    }
  }
}


void WifiTrack::processSighting(WTMacPtr aMac, WTSSidPtr aSSid, bool aNewSSidForMac)
{
  WTPersonPtr person = aMac->person; // default to already existing, if any
  // log
  if (FOCUSLOGENABLED) {
    string s;
    const char* sep = "";
    for (WTSSidSet::iterator pos = aMac->ssids.begin(); pos!=aMac->ssids.end(); ++pos) {
      string sstr = (*pos)->ssid;
      if (sstr.empty()) sstr = "<undefined>";
      string_format_append(s, "%s%s (%ld)", sep, sstr.c_str(), (*pos)->seenCount);
      sep = ", ";
    }
    FOCUSLOG("Sighted%s: MAC=%s (%ld), RSSI=%d,%d,%d : %s", person ? " and already has person" : "", macAddressToString(aMac->mac,':').c_str(), aMac->seenCount, aMac->worstRssi, aMac->lastRssi, aMac->bestRssi, s.c_str());
  }
  // process
  if (aNewSSidForMac && aSSid->macs.size()<tooCommonMacCount) {
    // a new SSID for this Mac, not too commonly used
    FOCUSLOG("- not too common (only %lu macs)", aSSid->macs.size());
    WTMacSet relatedMacs;
    WTMacPtr mostCommonMac;
    WTPersonPtr mostProbablePerson;
    if (aMac->ssids.size()>=minCommonSsidCount) {
      // has enough ssids overall -> try to find related MACs
      // - search all macs that know the new ssid
      int maxCommonSsids = 0;
      for (WTMacSet::iterator mpos = aSSid->macs.begin(); mpos!=aSSid->macs.end(); ++mpos) {
        // - see how many other ssids this mac shares with the other one
        if (*mpos==aMac) continue; // avoid comparing with myself!
        if ((*mpos)->ssids.size()<minCommonSsidCount) continue; // shortcut, candidate does not have enough ssids to possibly match at all -> next
        int commonSsids = 1; // we have at least aSSid in common by definition when we get here!
        for (WTSSidSet::iterator spos = (*mpos)->ssids.begin(); spos!=(*mpos)->ssids.end(); ++spos) {
          if (*spos==aSSid) continue; // shortcut, we know that we have aSSid in common
  //        if ((*spos)->macs.size()>=tooCommonMacCount) continue; // this is a too common ssid, don't count (maybe we still should %%%)
          if (aMac->ssids.find(*spos)!=aMac->ssids.end()) {
            commonSsids++;
          }
        }
        if (commonSsids<minCommonSsidCount) continue; // not a candidate
        LOG(LOG_INFO, "- This MAC %s has %d SSIDs in common with %s -> link to same person",
          macAddressToString(aMac->mac,':').c_str(),
          commonSsids,
          macAddressToString((*mpos)->mac,':').c_str()
        );
        relatedMacs.insert(*mpos); // is a candidate
        if (commonSsids>maxCommonSsids) {
          mostCommonMac = *mpos; // this is the mac with most common ssids
          if (mostCommonMac->person) mostProbablePerson = mostCommonMac->person; // this is the person of the mac with the most common ssids -> most likely the correct one
        }
      }
    }
    // determine person
    if (!person) {
      if (mostProbablePerson) {
        person = mostProbablePerson;
      }
      else {
        // none of the related macs has a person, or we have no related macs at all -> we need to create a person
        person = WTPersonPtr(new WTPerson);
        persons.insert(person);
        person->imageIndex = rand() % numPersonImages;
        person->color = hsbToPixel(rand() % 360);
        // link to this mac (without logging, as this happens for every new Mac seen)
        aMac->person = person;
        person->macs.insert(aMac);
      }
    }
    if (person) {
      // assign to all macs found related
      if (person->macs.insert(aMac).second) {
        LOG(LOG_NOTICE, "+++ Just sighted MAC %s via '%s' -> now linked to person '%s' (%d/#%s), MACs=%lu",
          macAddressToString(aMac->mac,':').c_str(),
          aSSid->ssid.c_str(),
          person->name.c_str(),
          person->imageIndex,
          pixelToWebColor(person->color).c_str(),
          person->macs.size()
        );
      }
      for (WTMacSet::iterator mpos = relatedMacs.begin(); mpos!=relatedMacs.end(); ++mpos) {
        (*mpos)->person = person;
        if (person->macs.insert(*mpos).second) {
          LOG(LOG_NOTICE, "+++ Found other MAC %s related -> now linked to person '%s' (%d/#%s), macs=%lu",
            macAddressToString((*mpos)->mac,':').c_str(),
            person->name.c_str(),
            person->imageIndex,
            pixelToWebColor(person->color).c_str(),
            person->macs.size()
          );
        }
      }
    }
  }
  // person determined, if any
  if (person) {
    // seen the person, update it
    person->seenCount++;
    person->seenLast = aMac->seenLast;
    person->lastRssi = aMac->lastRssi;
    if (person->bestRssi<person->lastRssi) person->bestRssi = person->lastRssi;
    if (person->worstRssi>person->lastRssi) person->worstRssi = person->lastRssi;
    if (person->seenFirst==Never) person->seenFirst = person->seenLast;
    LOG(LOG_INFO, "*** Recognized person%s, '%s', (%d/#%s), linked macs=%lu, via ssid='%s', mac=%s%s",
      person->hidden ? " (hidden)" : "",
      person->name.c_str(),
      person->imageIndex,
      pixelToWebColor(person->color).c_str(),
      person->macs.size(),
      aSSid->ssid.c_str(),
      macAddressToString(aMac->mac,':').c_str(),
      aMac->hidden ? " (hidden)" : ""
    );
    // show person?
    if (!aMac->hidden && !person->hidden && person->seenLast>person->shownLast+minShowInterval) {
      // determine name
      string nameToShow = person->name;
      if (nameToShow.empty()) {
        // pick SSID with the least mac links as most relevant (because: unique) name
        long minMacs = 999999999;
        WTSSidPtr relevantSSid;
        for (WTSSidSet::iterator pos = aMac->ssids.begin(); pos!=aMac->ssids.end(); ++pos) {
          if (!(*pos)->hidden && (*pos)->macs.size()<minMacs && !(*pos)->ssid.empty()) {
            minMacs = (*pos)->seenCount;
            relevantSSid = (*pos);
          }
        }
        LOG(LOG_DEBUG, "minMacs = %ld, relevantSSid='%s'", minMacs, relevantSSid ? relevantSSid->ssid.c_str() : "<none>");
        if (relevantSSid) {
          nameToShow = relevantSSid->ssid;
        }
      }
      // compose message
      string msg = string_format("P%d_%s - %s", person->imageIndex, pixelToWebColor(person->color).c_str(), nameToShow.c_str());
      // show message
      person->shownLast = person->seenLast;
      LOG(LOG_NOTICE, "*** Showing person '%s' (%d/#%s) via %s / '%s' : %s",
        person->name.c_str(),
        person->imageIndex,
        pixelToWebColor(person->color).c_str(),
        macAddressToString(aMac->mac,':').c_str(),
        aSSid->ssid.c_str(),
        msg.c_str()
      );
      JsonObjectPtr cmd = JsonObject::newObj();
      cmd->add("feature", JsonObject::newString("text"));
      cmd->add("text", JsonObject::newString(" "+msg));
      LethdApi::sharedApi()->executeJson(cmd);
      LethdApi::sharedApi()->runJsonScript("scripts/showssid.json", NULL, &scriptContext);
    }
  }
}
