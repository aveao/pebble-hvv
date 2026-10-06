var hmac = require('./hmac');
var buildConfig = require('./build_config');

// Pebble.getWatchToken() returns MD5(serial + appUuid + salt) as lowercase
// hex (see coredevices/mobileapp JsTokenUtil.kt). 32 hex chars.
var TOKEN_RE = /^[a-f0-9]{32}$/;

var DEMO_MODE = 'DEMO_MODE';

// Without a timeout a stalled request would leave the watch loading forever
var REQUEST_TIMEOUT_MS = 15000;

var cachedToken = null;
var cachedTokenResolved = false;

function getWatchToken() {
  if (cachedTokenResolved) return cachedToken;
  cachedTokenResolved = true;
  try {
    var t = Pebble.getWatchToken && Pebble.getWatchToken();
    if (typeof t === 'string' && TOKEN_RE.test(t)) {
      cachedToken = t;
    } else {
      cachedToken = null;
    }
  } catch (e) {
    cachedToken = null;
  }
  return cachedToken;
}

function pickAdapter() {
  var user = localStorage.getItem('gti_user');
  var password = localStorage.getItem('gti_password');
  if (user && password) return 'gti';

  var base = buildConfig.PROXY_API_BASE;
  var secret = buildConfig.PROXY_SECRET;
  if (base && secret && getWatchToken()) return 'proxy';

  return 'demo';
}

function getMode() {
  return pickAdapter();
}

// ---- adapters ----

// POSTs bodyStr and maps the response to callback(json, null) or
// callback(null, errorMessage). Shared by the GTI and proxy adapters.
function postJson(label, url, headers, bodyStr, cb) {
  // onerror and ontimeout can both fire for one request
  var done = false;
  function callback(json, err) {
    if (done) return;
    done = true;
    cb(json, err);
  }

  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.timeout = REQUEST_TIMEOUT_MS;
  for (var name in headers) {
    req.setRequestHeader(name, headers[name]);
  }

  req.onload = function() {
    console.log(label + ' response status=' + req.status);
    if (req.status === 200) {
      // Call back outside the try, so an exception in the caller isn't
      // reported as a second, parse-error callback
      var json;
      try { json = JSON.parse(req.responseText); }
      catch (e) { return callback(null, 'Parse error: ' + e.message); }
      if (!json || typeof json !== 'object') return callback(null, 'Parse error');
      callback(json, null);
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
  req.ontimeout = function() { callback(null, 'Timeout'); };
  req.send(bodyStr);
}

function requestGti(endpoint, body, callback) {
  var user = localStorage.getItem('gti_user');
  var password = localStorage.getItem('gti_password');
  var bodyStr = JSON.stringify(body);

  console.log('GTI request: ' + endpoint + ' body=' + bodyStr);
  postJson('GTI ' + endpoint, 'https://gti.geofox.de/gti/public/' + endpoint, {
    'Content-Type': 'application/json;charset=UTF-8',
    'Accept': 'application/json',
    'geofox-auth-user': user,
    'geofox-auth-signature': hmac.signRequest(password, bodyStr),
    'geofox-auth-type': 'HmacSHA1',
  }, bodyStr, callback);
}

function requestProxy(endpoint, body, callback) {
  var token = getWatchToken();
  if (!token) {
    callback(null, 'Service config error');
    return;
  }

  console.log('Proxy request: ' + endpoint);
  postJson('Proxy ' + endpoint, buildConfig.PROXY_API_BASE.replace(/\/$/, '') + '/' + endpoint, {
    'Content-Type': 'application/json',
    'Authorization': 'Bearer ' + buildConfig.PROXY_SECRET,
    'X-Watch-Token': token,
  }, JSON.stringify(body), callback);
}

// Demo data lives in index.js; the demo adapter is a no-op caller that
// returns a sentinel error so callers can route to demo fixtures themselves.
function requestDemo(endpoint, body, callback) {
  callback(null, DEMO_MODE);
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
  getWatchToken: getWatchToken,
  DEMO_MODE: DEMO_MODE,
};
