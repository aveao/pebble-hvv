var Clay = require('pebble-clay');
var clayConfig = require('./clay_config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var keys = require('message_keys');
var api = require('./api');

// Transit type enum matching C side
var TRANSIT_BUS = 0;
var TRANSIT_SBAHN = 1;
var TRANSIT_UBAHN = 2;
var TRANSIT_FERRY = 3;
var TRANSIT_UNKNOWN = 4;

// Configurable limits (loaded from localStorage, defaults below)
var MAX_NEARBY = parseInt(localStorage.getItem('max_nearby'), 10) || 3;
var MAX_DEPARTURES = parseInt(localStorage.getItem('max_departures'), 10) || 10;

// Aplite/diorite use a 2 KB AppMessage inbox (see comm.c), which can't fit
// 30 departures. Must match MAX_DEPARTURES in data.h.
var MAX_DEPARTURES_LIMIT_COLOR = 30;
var MAX_DEPARTURES_LIMIT_BW = 15;

function maxDeparturesLimit() {
  try {
    var platform = Pebble.getActiveWatchInfo().platform;
    if (platform && platform !== 'aplite' && platform !== 'diorite') {
      return MAX_DEPARTURES_LIMIT_COLOR;
    }
  } catch (e) {
    // Unknown platform: fall back to the limit that fits everywhere
  }
  return MAX_DEPARTURES_LIMIT_BW;
}

function getMaxDepartures() {
  return Math.min(MAX_DEPARTURES, maxDeparturesLimit());
}

// Demo departure data
var DEMO_DEPARTURES = [
  { line: 'U1',  type: TRANSIT_UBAHN, direction: 'Ohlstedt',         minutes: 2,  delay: 0 },
  { line: 'U1',  type: TRANSIT_UBAHN, direction: 'Norderstedt Mitte', minutes: 4,  delay: 1 },
  { line: 'S3',  type: TRANSIT_SBAHN, direction: 'Pinneberg',         minutes: 5,  delay: 0 },
  { line: 'U3',  type: TRANSIT_UBAHN, direction: 'Barmbek',           minutes: 8,  delay: 0 },
  { line: '112', type: TRANSIT_BUS,   direction: 'Mundsburg',         minutes: 7,  delay: 3 },
  { line: '62',  type: TRANSIT_FERRY, direction: 'Finkenwerder',      minutes: 10, delay: 0 },
  { line: 'S1',  type: TRANSIT_SBAHN, direction: 'Airport',           minutes: 12, delay: 2 },
  { line: '5',   type: TRANSIT_BUS,   direction: 'Burgwedel',         minutes: 14, delay: 0 },
  { line: 'U2',  type: TRANSIT_UBAHN, direction: 'Niendorf Markt',    minutes: 15, delay: 0 },
  { line: '73',  type: TRANSIT_FERRY, direction: 'Arningstr.',        minutes: 18, delay: 1 },
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

// Demo nearby stations when no credentials
var DEMO_NEARBY = [
  { name: 'Jungdemostieg', dist: 12, services: SERVICE_SBAHN | SERVICE_UBAHN | SERVICE_BUS },
  { name: 'Hauptdemohof', dist: 35, services: SERVICE_SBAHN | SERVICE_UBAHN | SERVICE_BUS | SERVICE_TRAIN },
  { name: 'Demohaus', dist: 45, services: SERVICE_UBAHN | SERVICE_BUS },
];

function fetchStations() {
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
    if (!lat || !lon) {
      if (favorites.length === 0) sendStationList([], favorites);
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
      // Favorites (if any) were already sent above; otherwise send an empty
      // list so the watch leaves its loading state.
      if (err) {
        console.log('checkName error: ' + err);
        if (favorites.length === 0) sendStationList([], favorites);
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
function sendDepartures(departures, station) {
  var dict = {};
  var count = Math.min(departures.length, getMaxDepartures());
  dict[keys.DEP_COUNT] = count;
  dict[keys.DEP_STATION] = station;

  for (var i = 0; i < count; i++) {
    var dep = departures[i];
    dict[keys.DEP_LINE + i]  = dep.line;
    dict[keys.DEP_TYPE + i]  = dep.type;
    dict[keys.DEP_DIR + i]   = dep.direction;
    dict[keys.DEP_MINS + i]  = dep.minutes;
    dict[keys.DEP_DELAY + i] = dep.delay;
    // Only sent when set, to keep messages small; the watch defaults to false
    if (dep.cancelled) dict[keys.DEP_CANCELLED + i] = 1;
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
    maxList: getMaxDepartures(),
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
      for (var i = 0; i < resp.departures.length && i < getMaxDepartures(); i++) {
        var d = resp.departures[i];
        var lineName = (d.line && d.line.name) ? d.line.name.replace(/-SEV$/, '').replace(/-BUS$/, '') : '?';
        var lineType = mapLineType(d.line);
        var dir = (d.line && d.line.direction) || d.direction || '';
        departures.push({
          line: lineName,
          type: lineType,
          direction: dir,
          minutes: d.timeOffset || 0,
          // GTI reports delay in seconds; the watch works in minutes
          delay: Math.round((d.delay || 0) / 60),
          cancelled: !!d.cancelled,
        });
      }
      sendDepartures(departures, station);
    } else {
      sendDepartures([], station);
    }
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

Pebble.addEventListener('showConfiguration', function() {
  var token = api.getWatchToken() || '(unavailable on this watch)';
  var configCopy = JSON.parse(JSON.stringify(clayConfig));
  substituteTokenPlaceholder(configCopy, token);
  setMaxDeparturesLimit(configCopy, maxDeparturesLimit());
  var dynamicClay = new Clay(configCopy, null, { autoHandleEvents: false });
  Pebble.openURL(dynamicClay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) return;

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

  // Refresh station list
  fetchStations();
});
