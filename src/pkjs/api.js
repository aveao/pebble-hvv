var hmac = require('./hmac');
var buildConfig = require('./build_config');

var SERIAL_RE = /^[A-Z0-9]{12}$/;

var cachedSerial = null;
var cachedSerialResolved = false;

function getSerial() {
  if (cachedSerialResolved) return cachedSerial;
  cachedSerialResolved = true;
  try {
    var info = Pebble.getActiveWatchInfo();
    var s = info && info.serialNumber;
    if (typeof s === 'string' && SERIAL_RE.test(s)) {
      cachedSerial = s;
    } else {
      cachedSerial = null;
    }
  } catch (e) {
    cachedSerial = null;
  }
  return cachedSerial;
}

function pickAdapter() {
  var user = localStorage.getItem('gti_user');
  var password = localStorage.getItem('gti_password');
  if (user && password) return 'gti';

  var base = buildConfig.PROXY_API_BASE;
  var secret = buildConfig.PROXY_SECRET;
  if (base && secret && getSerial()) return 'proxy';

  return 'demo';
}

function getMode() {
  return pickAdapter();
}

// ---- adapters ----

function requestGti(endpoint, body, callback) {
  var user = localStorage.getItem('gti_user');
  var password = localStorage.getItem('gti_password');
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
    if (req.status === 200) {
      try { callback(JSON.parse(req.responseText), null); }
      catch (e) { callback(null, 'Parse error: ' + e.message); }
    } else {
      callback(null, 'API error ' + req.status);
    }
  };
  req.onerror = function() { callback(null, 'Connection error'); };
  req.send(bodyStr);
}

function requestProxy(endpoint, body, callback) {
  var bodyStr = JSON.stringify(body);
  var serial = getSerial();
  var url = buildConfig.PROXY_API_BASE.replace(/\/$/, '') + '/' + endpoint;

  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.setRequestHeader('Authorization', 'Bearer ' + buildConfig.PROXY_SECRET);
  req.setRequestHeader('X-Watch-Serial', serial);

  console.log('Proxy request: ' + endpoint);
  req.onload = function() {
    console.log('Proxy response: ' + endpoint + ' status=' + req.status);
    if (req.status === 200) {
      try { callback(JSON.parse(req.responseText), null); }
      catch (e) { callback(null, 'Parse error: ' + e.message); }
    } else if (req.status === 429) {
      callback(null, 'Rate limited');
    } else if (req.status === 502) {
      callback(null, 'HVV unavailable');
    } else if (req.status === 401 || req.status === 400) {
      callback(null, 'Service config error');
    } else {
      callback(null, 'API error ' + req.status);
    }
  };
  req.onerror = function() { callback(null, 'Connection error'); };
  req.send(bodyStr);
}

// Demo data lives in index.js; the demo adapter is a no-op caller that
// returns a sentinel error so callers can route to demo fixtures themselves.
function requestDemo(endpoint, body, callback) {
  callback(null, 'DEMO_MODE');
}

function request(endpoint, body, callback) {
  var adapter = pickAdapter();
  if (adapter === 'gti')   return requestGti(endpoint, body, callback);
  if (adapter === 'proxy') return requestProxy(endpoint, body, callback);
  return requestDemo(endpoint, body, callback);
}

module.exports = {
  request: request,
  getMode: getMode,
};
