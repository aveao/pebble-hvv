var Clay = require('@rebble/clay');
var clayConfig = require('./clay_config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var keys = require('message_keys');
var api = require('./api');
var course = require('./course');
var truncateUtf8 = course.truncateUtf8;

// Transit type enum matching C side
var TRANSIT_BUS = 0;
var TRANSIT_SBAHN = 1;
var TRANSIT_UBAHN = 2;
var TRANSIT_FERRY = 3;
var TRANSIT_UNKNOWN = 4;

// GTI directionId values, matching the C DirectionId enum
var DIRECTION_FORWARD = 1;
var DIRECTION_BACKWARD = 6;

// Configurable limits (loaded from localStorage, defaults below)
var MAX_NEARBY = parseInt(localStorage.getItem('max_nearby'), 10) || 3;
var MAX_DEPARTURES = parseInt(localStorage.getItem('max_departures'), 10) || 10;

// Aplite/diorite use a 2 KB AppMessage inbox (see comm.c), which can't fit
// 30 departures. Must match MAX_DEPARTURES in data.h.
var MAX_DEPARTURES_LIMIT_COLOR = 30;
var MAX_DEPARTURES_LIMIT_BW = 15;

// The watch keeps DIRECTION_LEN - 1 bytes of each direction (see data.h)
var DIRECTION_MAX_BYTES = 31;

function getPlatform() {
  try {
    return Pebble.getActiveWatchInfo().platform || null;
  } catch (e) {
    return null;
  }
}

function maxDeparturesLimit() {
  var platform = getPlatform();
  if (platform && platform !== 'aplite' && platform !== 'diorite') {
    return MAX_DEPARTURES_LIMIT_COLOR;
  }
  // Unknown platform: fall back to the limit that fits everywhere
  return MAX_DEPARTURES_LIMIT_BW;
}

function getMaxDepartures() {
  return Math.min(MAX_DEPARTURES, maxDeparturesLimit());
}

// Demo departure data
var DEMO_DEPARTURES = [
  { line: 'U1',  type: TRANSIT_UBAHN, direction: 'Ohlstedt',         minutes: 2,  delay: 0, directionId: DIRECTION_FORWARD },
  { line: 'U1',  type: TRANSIT_UBAHN, direction: 'Norderstedt Mitte', minutes: 4,  delay: 1, directionId: DIRECTION_BACKWARD },
  { line: 'S3',  type: TRANSIT_SBAHN, direction: 'Pinneberg',         minutes: 5,  delay: 0, directionId: DIRECTION_BACKWARD },
  { line: 'U3',  type: TRANSIT_UBAHN, direction: 'Barmbek',           minutes: 8,  delay: 0, directionId: DIRECTION_BACKWARD },
  { line: '112', type: TRANSIT_BUS,   direction: 'Mundsburg',         minutes: 7,  delay: 3, directionId: DIRECTION_FORWARD },
  { line: '62',  type: TRANSIT_FERRY, direction: 'Finkenwerder',      minutes: 10, delay: 0, directionId: DIRECTION_BACKWARD },
  { line: 'S1',  type: TRANSIT_SBAHN, direction: 'Airport',           minutes: 12, delay: 2, directionId: DIRECTION_FORWARD },
  { line: '5',   type: TRANSIT_BUS,   direction: 'Burgwedel',         minutes: 14, delay: 0, directionId: DIRECTION_BACKWARD },
  { line: 'U2',  type: TRANSIT_UBAHN, direction: 'Niendorf Markt',    minutes: 15, delay: 0, directionId: DIRECTION_BACKWARD },
  { line: '73',  type: TRANSIT_FERRY, direction: 'Arningstr.',        minutes: 18, delay: 1, directionId: DIRECTION_FORWARD },
];

// ---- Helpers ----

function mapLineType(lineObj) {
  if (!lineObj || !lineObj.type) return TRANSIT_UNKNOWN;
  var lt = lineObj.type;
  var shortInfo = (lt.shortInfo || '').toUpperCase();
  var longInfo = (lt.longInfo || '').toUpperCase();
  if (shortInfo === 'S' || longInfo.indexOf('S-BAHN') >= 0) return TRANSIT_SBAHN;
  if (shortInfo === 'U' || longInfo.indexOf('U-BAHN') >= 0) return TRANSIT_UBAHN;
  if (shortInfo === 'BUS' || longInfo.indexOf('BUS') >= 0) return TRANSIT_BUS;
  if (longInfo.indexOf('FÄHRE') >= 0 || longInfo.indexOf('FAEHRE') >= 0 || longInfo.indexOf('SCHIFF') >= 0) return TRANSIT_FERRY;
  return TRANSIT_UNKNOWN;
}

// Emulator fallback coordinates: Hamburg Hbf
var EMULATOR_LAT = 53.55255;
var EMULATOR_LON = 10.0067347;

function isInHamburg(lat, lon) {
  // HVV covers Hamburg + surrounding region (roughly Cuxhaven to Lüneburg)
  return lat >= 53.0 && lat <= 54.0 && lon >= 8.8 && lon <= 10.8;
}

function isEmulator() {
  try {
    var info = Pebble.getActiveWatchInfo();
    return info && info.model && info.model.indexOf('qemu') >= 0;
  } catch (e) {
    return false;
  }
}

function getLocation(callback) {
  var done = false;
  function finish(lat, lon) {
    if (done) return;
    done = true;
    callback(lat, lon);
  }

  // Manual timeout in case geolocation never responds
  setTimeout(function() {
    if (!done) {
      if (isEmulator()) {
        console.log('Geolocation timed out (emulator), using Hbf');
        finish(EMULATOR_LAT, EMULATOR_LON);
      } else {
        console.log('Geolocation timed out');
        finish(null, null);
      }
    }
  }, 12000);

  try {
    navigator.geolocation.getCurrentPosition(function(pos) {
      var lat = pos.coords.latitude;
      var lon = pos.coords.longitude;
      console.log('Got GPS: ' + lat + ', ' + lon);
      if (isInHamburg(lat, lon)) {
        finish(lat, lon);
      } else if (isEmulator()) {
        console.log('Emulator outside Hamburg, using Hbf fallback');
        finish(EMULATOR_LAT, EMULATOR_LON);
      } else {
        console.log('Outside Hamburg area, no nearby stops');
        finish(null, null);
      }
    }, function(err) {
      if (isEmulator()) {
        console.log('Geolocation error (emulator), using Hbf');
        finish(EMULATOR_LAT, EMULATOR_LON);
      } else {
        console.log('Geolocation error: ' + (err && err.message));
        finish(null, null);
      }
    }, { timeout: 10000 });
  } catch (e) {
    if (isEmulator()) {
      console.log('Geolocation exception (emulator), using Hbf');
      finish(EMULATOR_LAT, EMULATOR_LON);
    } else {
      console.log('Geolocation exception: ' + e.message);
      finish(null, null);
    }
  }
}

function getFavorites() {
  var favs = [];
  for (var i = 1; i <= 5; i++) {
    var name = localStorage.getItem('fav_' + i);
    if (name && name.trim()) {
      favs.push(name.trim());
    }
  }
  return favs;
}

// Service type bitmask flags (must match C side)
var SERVICE_BUS   = (1 << 0);
var SERVICE_SBAHN = (1 << 1);
var SERVICE_UBAHN = (1 << 2);
var SERVICE_FERRY = (1 << 3);
var SERVICE_ABAHN = (1 << 4);
var SERVICE_TRAIN = (1 << 5);

function encodeServices(serviceTypes) {
  if (!serviceTypes) return 0;
  var mask = 0;
  for (var i = 0; i < serviceTypes.length; i++) {
    var s = serviceTypes[i].toLowerCase();
    if (s === 'bus' || s === 'fbus' || s === 'schnellbus') mask |= SERVICE_BUS;
    else if (s === 'sbahn' || s === 's') mask |= SERVICE_SBAHN;
    else if (s === 'ubahn' || s === 'u') mask |= SERVICE_UBAHN;
    else if (s === 'faehre' || s === 'ship') mask |= SERVICE_FERRY;
    else if (s === 'abahn' || s === 'a') mask |= SERVICE_ABAHN;
    else if (s === 'train' || s === 'r' || s === 'rbahn' || s === 'rb' || s === 're' || s === 'ice' || s === 'fbahn') mask |= SERVICE_TRAIN;
  }
  return mask;
}

// ---- Messaging ----

var SEND_RETRIES = 2;
var SEND_RETRY_DELAY_MS = 1000;

// Latest send number per message kind, so a retry never overwrites a
// newer message of the same kind (e.g. favorites-only over the nearby list)
var latestSend = {};

// The watch NACKs messages that collide with one it is sending, so retry
// a couple of times rather than leaving it waiting for the next refresh.
function sendToWatch(dict, kind) {
  var seq = latestSend[kind] = (latestSend[kind] || 0) + 1;
  var attempt = 0;
  function send() {
    Pebble.sendAppMessage(dict, function() {
      console.log(kind + ' sent to watch');
    }, function(e) {
      console.log('Failed to send ' + kind + ': ' + JSON.stringify(e));
      if (attempt < SEND_RETRIES && latestSend[kind] === seq) {
        attempt++;
        setTimeout(function() {
          if (latestSend[kind] === seq) send();
        }, SEND_RETRY_DELAY_MS);
      }
    });
  }
  send();
}

// ---- Station List ----

function sendStationList(nearby, favorites) {
  var dict = {};
  var stations = [];

  // Add nearby (up to 3)
  for (var i = 0; i < nearby.length && i < MAX_NEARBY; i++) {
    stations.push({ name: nearby[i].name, isFav: 0, dist: nearby[i].dist, services: nearby[i].services || 0 });
  }
  // Add favorites
  for (var j = 0; j < favorites.length && stations.length < 15; j++) {
    stations.push({ name: favorites[j], isFav: 1, dist: 0, services: 0 });
  }

  dict[keys.STATION_COUNT] = stations.length;
  for (var k = 0; k < stations.length; k++) {
    dict[keys.STATION_NAME + k] = stations[k].name;
    dict[keys.STATION_IS_FAV + k] = stations[k].isFav;
    dict[keys.STATION_DIST + k] = stations[k].dist;
    dict[keys.STATION_SERVICES + k] = stations[k].services;
  }

  sendToWatch(dict, 'Station list');
}

// Tell the watch the stop lookup failed, so it can offer a retry instead of
// showing an empty list. Same kind as the list: whichever is newest wins.
function sendStationsError(msg) {
  var dict = {};
  dict[keys.STATIONS_ERROR] = msg;
  sendToWatch(dict, 'Station list');
}

// Demo nearby stations when no credentials
var DEMO_NEARBY = [
  { name: 'Jungdemostieg', dist: 12, services: SERVICE_SBAHN | SERVICE_UBAHN | SERVICE_BUS },
  { name: 'Hauptdemohof', dist: 35, services: SERVICE_SBAHN | SERVICE_UBAHN | SERVICE_BUS | SERVICE_TRAIN },
  { name: 'Demohaus', dist: 45, services: SERVICE_UBAHN | SERVICE_BUS },
];

// Bumped on each fetchStations call, so a slow GPS or checkName callback from
// an earlier call can't send its outdated list over a newer one (e.g. after
// favorites were edited)
var stationFetchGen = 0;

function fetchStations() {
  var gen = ++stationFetchGen;
  var favorites = getFavorites();
  var mode = api.getMode();

  if (mode === 'demo') {
    console.log('Demo mode, sending demo stations');
    sendStationList(DEMO_NEARBY, favorites);
    return;
  }

  // Send favorites immediately so user sees them while GPS resolves
  if (favorites.length > 0) {
    console.log('Sending favorites immediately (' + favorites.length + ')');
    sendStationList([], favorites);
  }

  // Fetch nearby stations asynchronously
  getLocation(function(lat, lon) {
    if (gen !== stationFetchGen) return;
    // Favorites (if any) were already sent above; otherwise report the
    // failure so the watch leaves its loading state
    if (!lat || !lon) {
      if (favorites.length === 0) sendStationsError('Location unavailable');
      return;
    }
    var checkNameBody = {
      version: 63,
      theName: {
        name: 'Haltestelle',
        type: 'STATION',
        coordinate: { x: lon, y: lat },
      },
      coordinateType: 'EPSG_4326',
      maxList: MAX_NEARBY,
      maxDistance: 2550,
      filterType: 'NO_FILTER',
      allowTypeSwitch: true,
    };
    api.request('checkName', checkNameBody, function(resp, err) {
      if (gen !== stationFetchGen) return;
      if (err) {
        console.log('checkName error: ' + err);
        if (favorites.length === 0) sendStationsError(err);
        return;
      }
      var results = resp.results || resp.sdNameList || [];
      if (!results.length) {
        console.log('checkName: no stations found');
        if (favorites.length === 0) sendStationList([], favorites);
        return;
      }
      var nearby = [];
      for (var i = 0; i < results.length && i < MAX_NEARBY; i++) {
        var r = results[i];
        if (r.type && r.type !== 'STATION') continue;
        var distMeters = r.distance || 0;
        nearby.push({
          name: r.name,
          dist: Math.min(Math.round(distMeters / 10), 255),
          services: encodeServices(r.serviceTypes),
        });
      }
      sendStationList(nearby, favorites);
    });
  });
}

// ---- Departures ----

// DEP_STATION echoes the requested station so the watch can drop responses
// that arrive after the user has moved on to another station.

// GTI's first N by scheduled time can miss departures that leave before a
// delayed one in that set, so fetch a few extra, sort, then cut to N.
var DEPARTURE_FETCH_EXTRA = 5;

// GTI lists departures by scheduled time, so a delayed service that was due a
// few minutes ago stays on top. Order by when they'll actually leave instead
// (scheduled + delay), keeping GTI's order for departures in the same minute
function sortByExpectedTime(departures) {
  return departures
    .map(function(dep, i) { return { dep: dep, i: i }; })
    .sort(function(a, b) {
      return (a.dep.minutes + a.dep.delay) - (b.dep.minutes + b.dep.delay) || a.i - b.i;
    })
    .map(function(entry) { return entry.dep; });
}

// The departure list last sent to the watch, so a tapped row's index maps
// back to its trip (see fetchCourse)
var lastDepartures = null;

function sendDepartures(departures, station) {
  var dict = {};
  var count = Math.min(departures.length, getMaxDepartures());
  lastDepartures = { station: station, deps: departures.slice(0, count) };
  dict[keys.DEP_COUNT] = count;
  dict[keys.DEP_STATION] = station;

  for (var i = 0; i < count; i++) {
    var dep = departures[i];
    dict[keys.DEP_LINE + i]  = dep.line;
    dict[keys.DEP_TYPE + i]  = dep.type;
    // Anything longer is dropped on the watch anyway, and long directions
    // can push the message past aplite's 2 KB inbox
    dict[keys.DEP_DIR + i]   = truncateUtf8(dep.direction, DIRECTION_MAX_BYTES);
    dict[keys.DEP_MINS + i]  = dep.minutes;
    dict[keys.DEP_DELAY + i] = dep.delay;
    // Only sent when set, to keep messages small; the watch defaults to false
    if (dep.cancelled) dict[keys.DEP_CANCELLED + i] = 1;
    if (dep.directionId) dict[keys.DEP_DIR_ID + i] = dep.directionId;
  }

  sendToWatch(dict, 'Departures');
}

function sendError(msg, station) {
  var dict = {};
  dict[keys.ERROR_MSG] = msg;
  dict[keys.DEP_STATION] = station;
  // Same kind as departures: whichever result is newest wins
  sendToWatch(dict, 'Departures');
}

function fetchDepartures(station) {
  console.log('fetchDepartures for: ' + station);

  var mode = api.getMode();
  if (mode === 'demo') {
    console.log('Demo mode, sending demo data');
    sendDepartures(DEMO_DEPARTURES, station);
    return;
  }

  api.request('departureList', {
    // GTI defaults to version 1, which lacks delay and cancelled (v19+)
    version: 63,
    station: { name: station, type: 'STATION' },
    time: { date: 'heute', time: 'jetzt' },
    maxList: getMaxDepartures() + DEPARTURE_FETCH_EXTRA,
    maxTimeOffset: 999,
    useRealtime: true,
  }, function(resp, err) {
    if (err) {
      console.log('departureList error: ' + err);
      sendError(err, station);
      return;
    }
    if (resp.departures && resp.departures.length > 0) {
      var departures = [];
      for (var i = 0; i < resp.departures.length; i++) {
        var d = resp.departures[i];
        var lineName = (d.line && d.line.name) ? d.line.name.replace(/-SEV$/, '').replace(/-BUS$/, '') : '?';
        var lineType = mapLineType(d.line);
        // departure.direction is documented as the numeric direction id;
        // only take it as the destination when it's text
        var dir = (d.line && d.line.direction) ||
          (typeof d.direction === 'string' ? d.direction : '');
        var planned = course.plannedTime(resp.time, d.timeOffset || 0);
        departures.push({
          line: lineName,
          type: lineType,
          direction: dir,
          minutes: d.timeOffset || 0,
          // GTI reports delay in seconds; the watch works in minutes
          delay: Math.round((d.delay || 0) / 60),
          cancelled: !!d.cancelled,
          // GTI's directionId (1 = forward, 6 = backward along the line); the
          // handbook documents it as departure.direction. The watch ignores
          // other values.
          directionId: typeof d.directionId === 'number' ? d.directionId : 0,
          // What departureCourse needs to find this trip
          trip: {
            serviceId: d.serviceId,
            lineId: d.line && d.line.dlid,
            lineKey: d.line && d.line.id,
            station: (d.station && d.station.id) ?
              { id: d.station.id, name: d.station.combinedName, type: 'STATION' } :
              { name: station, type: 'STATION' },
            time: planned && planned.time,
            plannedSecs: planned && planned.secs,
          },
        });
      }
      sendDepartures(sortByExpectedTime(departures), station);
    } else {
      sendDepartures([], station);
    }
  });
}

// ---- Route (departureCourse) ----

function sendCourse(result, reqId) {
  var dict = {};
  dict[keys.COURSE_REQ_ID] = reqId;
  dict[keys.COURSE_COUNT] = result.stops.length;
  dict[keys.COURSE_FOCUS] = result.focus;
  for (var i = 0; i < result.stops.length; i++) {
    var stop = result.stops[i];
    dict[keys.COURSE_STOP + i] = stop.name;
    dict[keys.COURSE_TIME + i] = stop.time;
    dict[keys.COURSE_DELAY + i] = stop.delay;
    // Only sent when set, to keep messages small
    if (stop.cancelled) dict[keys.COURSE_CANCELLED + i] = 1;
  }
  sendToWatch(dict, 'Course');
}

function sendCourseError(msg, reqId) {
  var dict = {};
  dict[keys.COURSE_REQ_ID] = reqId;
  dict[keys.COURSE_ERROR] = msg;
  sendToWatch(dict, 'Course');
}

// The watch keeps LINE_NAME_LEN - 1 bytes of each line name (see data.h)
var LINE_MAX_BYTES = 7;

// Route of the departure at index in the list last sent for station. The
// watch also sends the row's line name, so a list that changed since the
// tap isn't answered with some other trip's route.
function fetchCourse(station, index, line, reqId) {
  var dep = lastDepartures && lastDepartures.station === station &&
    lastDepartures.deps[index];
  if (!dep || truncateUtf8(dep.line, LINE_MAX_BYTES) !== line) {
    console.log('fetchCourse: no matching departure for ' + station + ' #' + index);
    sendCourseError('List changed, try again', reqId);
    return;
  }

  if (api.getMode() === 'demo') {
    sendCourse(course.demoCourse(dep, station, Date.now() / 1000), reqId);
    return;
  }

  var trip = dep.trip;
  if (!trip || typeof trip.serviceId !== 'number' || !trip.time) {
    sendCourseError('Route unavailable', reqId);
    return;
  }
  var body = {
    version: 63,
    serviceId: trip.serviceId,
    station: trip.station,
    time: trip.time,
    segments: 'ALL',
  };
  // GTI wants at least one of these
  if (trip.lineId) body.lineId = trip.lineId;
  if (trip.lineKey) body.lineKey = trip.lineKey;

  api.request('departureCourse', body, function(resp, err) {
    if (err) {
      console.log('departureCourse error: ' + err);
      sendCourseError(err, reqId);
      return;
    }
    var result = course.buildStops(resp, trip.station.id, trip.plannedSecs);
    if (!result) {
      // GTI errors come back as a returnCode + errorText (BYO), or as an
      // empty course (proxy)
      sendCourseError((resp && resp.errorText) || 'Route unavailable', reqId);
      return;
    }
    sendCourse(course.windowStops(result, course.MAX_COURSE_STOPS), reqId);
  });
}

// ---- Event Handlers ----

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  try {
    fetchStations();
  } catch (e) {
    console.log('fetchStations error: ' + e.message);
    sendStationList(DEMO_NEARBY, []);
  }
});

// Some phone apps key incoming payloads by numeric key, others by name.
// Checked with !== undefined, so a 0 value (e.g. row 0) isn't skipped.
function payloadValue(payload, name) {
  var value = payload[keys[name]];
  return value !== undefined ? value : payload[name];
}

Pebble.addEventListener('appmessage', function(e) {
  console.log('appmessage keys: ' + JSON.stringify(Object.keys(e.payload)));
  console.log('appmessage payload: ' + JSON.stringify(e.payload));

  var reqStations = e.payload[keys.REQUEST_STATIONS] || e.payload['REQUEST_STATIONS'];
  var reqDeps = e.payload[keys.REQUEST_DEPARTURES] || e.payload['REQUEST_DEPARTURES'];

  if (reqStations) {
    console.log('-> REQUEST_STATIONS');
    fetchStations();
  }
  // REQUEST_DEPARTURES carries the station name, so JS keeps no selection
  // state that could go stale (e.g. if the JS runtime restarts)
  if (typeof reqDeps === 'string' && reqDeps) {
    console.log('-> REQUEST_DEPARTURES: ' + reqDeps);
    fetchDepartures(reqDeps);
  }
  var reqCourse = payloadValue(e.payload, 'REQUEST_COURSE');
  if (typeof reqCourse === 'string' && reqCourse) {
    console.log('-> REQUEST_COURSE: ' + reqCourse);
    fetchCourse(reqCourse, payloadValue(e.payload, 'COURSE_DEP_INDEX'),
      payloadValue(e.payload, 'COURSE_DEP_LINE'), payloadValue(e.payload, 'COURSE_REQ_ID'));
  }
});

function substituteTokenPlaceholder(items, token) {
  for (var i = 0; i < items.length; i++) {
    var it = items[i];
    if (it && it.items) substituteTokenPlaceholder(it.items, token);
    if (it && typeof it.defaultValue === 'string' && it.defaultValue.indexOf('{{TOKEN}}') !== -1) {
      it.defaultValue = it.defaultValue.split('{{TOKEN}}').join(token);
    }
  }
}

function setMaxDeparturesLimit(items, limit) {
  for (var i = 0; i < items.length; i++) {
    var it = items[i];
    if (it && it.items) setMaxDeparturesLimit(it.items, limit);
    if (it && it.messageKey === 'CONFIG_MAX_DEPARTURES' && it.attributes) {
      it.attributes.max = limit;
    }
  }
}

var BOLD_TEXT_PLATFORMS = ['emery', 'gabbro'];

// Remove settings items by messageKey (e.g. options only one platform supports)
function removeConfigItems(items, messageKeys) {
  for (var i = items.length - 1; i >= 0; i--) {
    var it = items[i];
    if (it && it.items) removeConfigItems(it.items, messageKeys);
    if (it && messageKeys.indexOf(it.messageKey) !== -1) items.splice(i, 1);
  }
}

Pebble.addEventListener('showConfiguration', function() {
  var token = api.getWatchToken() || '(unavailable on this watch)';
  var configCopy = JSON.parse(JSON.stringify(clayConfig));
  substituteTokenPlaceholder(configCopy, token);
  setMaxDeparturesLimit(configCopy, maxDeparturesLimit());
  // Bold text only has layouts tuned for these watches
  if (BOLD_TEXT_PLATFORMS.indexOf(getPlatform()) === -1) {
    removeConfigItems(configCopy, ['CONFIG_BOLD_TEXT']);
  }
  var dynamicClay = new Clay(configCopy, null, { autoHandleEvents: false });
  Pebble.openURL(dynamicClay.generateUrl());
});

// Display settings the watch stores itself (see settings.c)
var WATCH_SETTINGS = ['CONFIG_BOLD_TEXT', 'CONFIG_TOUCH_NAV', 'CONFIG_DIRECTION_ARROWS'];

// Pick the watch-side settings out of Clay's result as 0/1, or null if none
// are present (e.g. the bold toggle is hidden on most watches)
function buildWatchSettings(dict) {
  var settings = null;
  for (var i = 0; i < WATCH_SETTINGS.length; i++) {
    var key = keys[WATCH_SETTINGS[i]];
    if (dict[key] !== undefined) {
      settings = settings || {};
      settings[key] = dict[key] ? 1 : 0;
    }
  }
  return settings;
}

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;

  var dict = clay.getSettings(e.response);

  // Save credentials. Empty string means the user cleared the field;
  // we must remove the stored value so pickAdapter falls back to proxy.
  var user = dict[keys.CONFIG_USER];
  var password = dict[keys.CONFIG_PASSWORD];
  if (user !== undefined) {
    if (user) localStorage.setItem('gti_user', user);
    else localStorage.removeItem('gti_user');
  }
  if (password !== undefined) {
    if (password) localStorage.setItem('gti_password', password);
    else localStorage.removeItem('gti_password');
  }

  // Save favorites
  for (var i = 1; i <= 5; i++) {
    var val = dict[keys['FAV_' + i]];
    if (val !== undefined) {
      localStorage.setItem('fav_' + i, val);
    }
  }

  // Save display settings
  var maxNearby = dict[keys.CONFIG_MAX_NEARBY];
  var maxDeps = dict[keys.CONFIG_MAX_DEPARTURES];
  if (maxNearby) {
    var n = Math.max(1, Math.min(10, parseInt(maxNearby, 10) || 3));
    localStorage.setItem('max_nearby', n);
    MAX_NEARBY = n;
  }
  if (maxDeps) {
    var d = Math.max(10, Math.min(maxDeparturesLimit(), parseInt(maxDeps, 10) || 10));
    localStorage.setItem('max_departures', d);
    MAX_DEPARTURES = d;
  }

  // The watch stores its own display settings
  var watchSettings = buildWatchSettings(dict);
  if (watchSettings) sendToWatch(watchSettings, 'Settings');

  // Refresh station list
  fetchStations();
});
