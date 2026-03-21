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

// Demo departure data - sent when no API credentials are configured
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

function mapLineType(lineObj) {
  // GTI line.type structure: {simpleType: "TRAIN", shortInfo: "S", longInfo: "S-Bahn"}
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

function sendDepartures(departures) {
  var dict = {};
  var count = Math.min(departures.length, 10);
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

function fetchFromGTI(user, password, station) {
  var body = JSON.stringify({
    station: { name: station, type: 'STATION' },
    time: { date: 'heute', time: 'jetzt' },
    maxList: 10,
    maxTimeOffset: 60,
    useRealtime: true
  });

  var signature = hmac.signRequest(password, body);

  var req = new XMLHttpRequest();
  req.open('POST', 'https://gti.geofox.de/gti/public/departureList', true);
  req.setRequestHeader('Content-Type', 'application/json;charset=UTF-8');
  req.setRequestHeader('Accept', 'application/json');
  req.setRequestHeader('geofox-auth-user', user);
  req.setRequestHeader('geofox-auth-signature', signature);
  req.setRequestHeader('geofox-auth-type', 'HmacSHA1');

  req.onload = function() {
    if (req.status === 200) {
      try {
        var resp = JSON.parse(req.responseText);
        if (resp.departures && resp.departures.length > 0) {
          var departures = [];
          console.log('First departure line obj: ' + JSON.stringify(resp.departures[0].line));
          for (var i = 0; i < resp.departures.length && i < 10; i++) {
            var d = resp.departures[i];
            departures.push({
              line: d.line ? d.line.name : '?',
              type: mapLineType(d.line),
              direction: (d.line && d.line.direction) || d.direction || '',
              minutes: d.timeOffset || 0,
              delay: d.delay || 0
            });
          }
          sendDepartures(departures);
        } else {
          sendDepartures([]);
        }
      } catch (e) {
        console.log('Parse error: ' + e.message);
        sendError('Parse error');
      }
    } else {
      console.log('GTI API error: ' + req.status);
      sendError('API error ' + req.status);
    }
  };

  req.onerror = function() {
    console.log('GTI request failed');
    sendError('Connection error');
  };

  req.send(body);
}

function fetchDepartures() {
  var user = localStorage.getItem('gti_user');
  var password = localStorage.getItem('gti_password');
  var station = localStorage.getItem('station') || 'Jungfernstieg';

  if (!user || !password) {
    console.log('No GTI credentials, sending demo data');
    sendDepartures(DEMO_DEPARTURES);
    return;
  }

  fetchFromGTI(user, password, station);
}

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  fetchDepartures();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload[keys.REQUEST_DEPARTURES]) {
    fetchDepartures();
  }
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) return;

  var dict = clay.getSettings(e.response);

  // Save config to localStorage for API calls
  var station = dict[keys.CONFIG_STATION];
  var user = dict[keys.CONFIG_USER];
  var password = dict[keys.CONFIG_PASSWORD];

  if (station) localStorage.setItem('station', station);
  if (user) localStorage.setItem('gti_user', user);
  if (password) localStorage.setItem('gti_password', password);

  // Send station name to watch for display
  var watchDict = {};
  if (station) watchDict[keys.CONFIG_STATION] = station;
  Pebble.sendAppMessage(watchDict, function() {
    // After config saved, fetch fresh data
    fetchDepartures();
  });
});
