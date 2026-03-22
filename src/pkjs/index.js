var Clay = require('pebble-clay');
var clayConfig = require('./clay_config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var keys = require('message_keys');
var hmac = require('./hmac');

// Transit type enum matching C side
var TRANSIT_BUS = 0;
var TRANSIT_SBAHN = 1;
var TRANSIT_UBAHN = 2;
var TRANSIT_FERRY = 3;
var TRANSIT_UNKNOWN = 4;

// Configurable limits (loaded from localStorage, defaults below)
var MAX_NEARBY = parseInt(localStorage.getItem('max_nearby'), 10) || 3;
var MAX_DEPARTURES = parseInt(localStorage.getItem('max_departures'), 10) || 10;

// Current station for departure fetches
var currentStation = null;

// Demo departure data
var DEMO_DEPARTURES = [
  { line: 'U1',  type: TRANSIT_UBAHN, direction: 'Ohlstedt',         minutes: 2,  delay: 0 },
  { line: 'U1',  type: TRANSIT_UBAHN, direction: 'Norderstedt Mitte', minutes: 4,  delay: 1 },
  { line: 'S3',  type: TRANSIT_SBAHN, direction: 'Pinneberg',         minutes: 5,  delay: 0 },
  { line: '112', type: TRANSIT_BUS,   direction: 'Mundsburg',         minutes: 7,  delay: 3 },
  { line: 'U3',  type: TRANSIT_UBAHN, direction: 'Barmbek',           minutes: 8,  delay: 0 },
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

function gtiRequest(endpoint, body, callback) {
  var user = localStorage.getItem('gti_user');
  var password = localStorage.getItem('gti_password');

  if (!user || !password) {
    callback(null, 'No credentials');
    return;
  }

  var bodyStr = JSON.stringify(body);
  var signature = hmac.signRequest(password, bodyStr);

  var req = new XMLHttpRequest();
  req.open('POST', 'https://gti.geofox.de/gti/public/' + endpoint, true);
  req.setRequestHeader('Content-Type', 'application/json;charset=UTF-8');
  req.setRequestHeader('Accept', 'application/json');
  req.setRequestHeader('geofox-auth-user', user);
  req.setRequestHeader('geofox-auth-signature', signature);
  req.setRequestHeader('geofox-auth-type', 'HmacSHA1');

  console.log('GTI request: ' + endpoint + ' body=' + bodyStr);

  req.onload = function() {
    console.log('GTI response: ' + endpoint + ' status=' + req.status);
    console.log('GTI ' + endpoint + ' response body: ' + req.responseText);
    if (req.status === 200) {
      try {
        var parsed = JSON.parse(req.responseText);
        callback(parsed, null);
      } catch (e) {
        callback(null, 'Parse error: ' + e.message);
      }
    } else {
      callback(null, 'API error ' + req.status);
    }
  };
  req.onerror = function() {
    console.log('GTI ' + endpoint + ' connection error');
    callback(null, 'Connection error');
  };
  req.send(bodyStr);
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

  Pebble.sendAppMessage(dict, function() {
    console.log('Station list sent (' + stations.length + ' stations)');
  }, function(e) {
    console.log('Failed to send station list: ' + JSON.stringify(e));
  });
}

// Demo nearby stations when no credentials
var DEMO_NEARBY = [
  { name: 'Jungfernstieg', dist: 12, services: SERVICE_SBAHN | SERVICE_UBAHN | SERVICE_BUS },
  { name: 'Hauptbahnhof', dist: 35, services: SERVICE_SBAHN | SERVICE_UBAHN | SERVICE_BUS | SERVICE_TRAIN },
  { name: 'Rathaus', dist: 45, services: SERVICE_UBAHN | SERVICE_BUS },
];

function fetchStations() {
  var favorites = getFavorites();
  var user = localStorage.getItem('gti_user');

  if (!user) {
    console.log('No credentials, sending demo stations');
    sendStationList(DEMO_NEARBY, favorites);
    return;
  }

  // Send favorites immediately so user sees them while GPS resolves
  if (favorites.length > 0) {
    console.log('Sending favorites immediately (' + favorites.length + ')');
    sendStationList([], favorites);
  }

  // Then fetch nearby stations asynchronously
  getLocation(function(lat, lon) {
    if (!lat || !lon) {
      // If no favorites were sent yet, send empty list so watch knows we're done
      if (favorites.length === 0) {
        sendStationList([], favorites);
      }
      return;
    }

    var checkNameBody = {
      version: 63,
      theName: {
        name: 'Haltestelle',
        type: 'STATION',
        coordinate: {
          x: lon,
          y: lat
        }
      },
      coordinateType: 'EPSG_4326',
      maxList: MAX_NEARBY,
      maxDistance: 2550,
      filterType: 'NO_FILTER',
      allowTypeSwitch: true
    };
    gtiRequest('checkName', checkNameBody, function(resp, err) {
      if (err) {
        console.log('checkName error: ' + err);
        // Favorites already sent, no need to resend empty nearby
        return;
      }

      var results = resp.results || resp.sdNameList || [];
      if (!results.length) {
        console.log('checkName: no stations found');
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
          services: encodeServices(r.serviceTypes)
        });
      }
      // Send complete list (favorites + nearby) to replace the favorites-only list
      sendStationList(nearby, favorites);
    });
  });
}

// ---- Departures ----

function sendDepartures(departures) {
  var dict = {};
  var count = Math.min(departures.length, MAX_DEPARTURES);
  dict[keys.DEP_COUNT] = count;

  for (var i = 0; i < count; i++) {
    var dep = departures[i];
    dict[keys.DEP_LINE + i]  = dep.line;
    dict[keys.DEP_TYPE + i]  = dep.type;
    dict[keys.DEP_DIR + i]   = dep.direction;
    dict[keys.DEP_MINS + i]  = dep.minutes;
    dict[keys.DEP_DELAY + i] = dep.delay;
  }

  Pebble.sendAppMessage(dict, function() {
    console.log('Departures sent to watch');
  }, function(e) {
    console.log('Failed to send departures: ' + JSON.stringify(e));
  });
}

function sendError(msg) {
  var dict = {};
  dict[keys.ERROR_MSG] = msg;
  Pebble.sendAppMessage(dict);
}

function fetchDepartures() {
  if (!currentStation) {
    console.log('No station selected');
    return;
  }

  console.log('fetchDepartures for: ' + currentStation);

  var user = localStorage.getItem('gti_user');
  var password = localStorage.getItem('gti_password');

  if (!user || !password) {
    console.log('No GTI credentials, sending demo data');
    sendDepartures(DEMO_DEPARTURES);
    return;
  }

  var stationObj = { name: currentStation, type: 'STATION' };
  console.log('departureList request station: ' + JSON.stringify(stationObj));

  gtiRequest('departureList', {
    station: stationObj,
    time: { date: 'heute', time: 'jetzt' },
    maxList: MAX_DEPARTURES,
    maxTimeOffset: 999,
    useRealtime: true
  }, function(resp, err) {
    if (err) {
      console.log('departureList error: ' + err);
      sendError(err);
      return;
    }
    if (resp.departures && resp.departures.length > 0) {
      var departures = [];
      for (var i = 0; i < resp.departures.length && i < MAX_DEPARTURES; i++) {
        var d = resp.departures[i];
        var lineName = d.line ? d.line.name.replace(/-SEV$/, '').replace(/-BUS$/, '') : '?';
        var lineType = mapLineType(d.line);
        var dir = (d.line && d.line.direction) || d.direction || '';
        departures.push({
          line: lineName,
          type: lineType,
          direction: dir,
          minutes: d.timeOffset || 0,
          delay: d.delay || 0
        });
      }
      sendDepartures(departures);
    } else {
      sendDepartures([]);
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

  var selectVal = e.payload[keys.SELECT_STATION] || e.payload['SELECT_STATION'];
  var reqStations = e.payload[keys.REQUEST_STATIONS] || e.payload['REQUEST_STATIONS'];
  var reqDeps = e.payload[keys.REQUEST_DEPARTURES] || e.payload['REQUEST_DEPARTURES'];

  if (reqStations) {
    console.log('-> REQUEST_STATIONS');
    fetchStations();
  }
  if (reqDeps) {
    console.log('-> REQUEST_DEPARTURES');
    fetchDepartures();
  }
  if (selectVal) {
    currentStation = selectVal;
    console.log('-> SELECT_STATION: ' + currentStation);
    fetchDepartures();
  }
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) return;

  var dict = clay.getSettings(e.response);

  // Save credentials
  var user = dict[keys.CONFIG_USER];
  var password = dict[keys.CONFIG_PASSWORD];
  if (user) localStorage.setItem('gti_user', user);
  if (password) localStorage.setItem('gti_password', password);

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
    var d = Math.max(10, Math.min(30, parseInt(maxDeps, 10) || 10));
    localStorage.setItem('max_departures', d);
    MAX_DEPARTURES = d;
  }

  // Refresh station list
  fetchStations();
});
