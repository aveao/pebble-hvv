// Helpers for a departure's route (GTI departureCourse). Pure functions, so
// they can be tested with plain Node.

// The watch keeps 64 stops (COURSE_STOP[64] in package.json)
var MAX_COURSE_STOPS = 64;
// Stops kept before the user's stop when a route is longer than that
var STOPS_BEFORE_FOCUS = 16;
// The watch keeps 31 bytes of each name (see data.h / course.h)
var NAME_MAX_BYTES = 31;

var DAY_MS = 86400000;
var HOUR_MS = 3600000;

// Cut str to at most maxBytes of UTF-8 without splitting a character
function truncateUtf8(str, maxBytes) {
  var bytes = 0;
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i);
    var size = c < 0x80 ? 1 : c < 0x800 ? 2 : (c >= 0xD800 && c <= 0xDBFF) ? 4 : 3;
    if (bytes + size > maxBytes) return str.substring(0, i);
    bytes += size;
    // A surrogate pair is one 4-byte character
    if (size === 4) i++;
  }
  return str;
}

function pad2(n) {
  return (n < 10 ? '0' : '') + n;
}

// 01:00 UTC on the last Sunday of the month (month 0-based), when EU
// daylight saving time starts (March) or ends (October)
function dstSwitch(year, month) {
  var lastDay = Date.UTC(year, month + 1, 0);
  return lastDay - new Date(lastDay).getUTCDay() * DAY_MS + HOUR_MS;
}

// Hamburg's UTC offset at the UTC instant ms, as GTI writes it ('+0200')
function hamburgOffset(ms) {
  var year = new Date(ms).getUTCFullYear();
  var summer = ms >= dstSwitch(year, 2) && ms < dstSwitch(year, 9);
  return summer ? '+0200' : '+0100';
}

// Planned departure as a GTI dateTime, from a departureList response's
// time ({date: 'dd.mm.yyyy', time: 'HH:MM'}, Hamburg local) plus the
// departure's timeOffset in minutes. Returns {time, secs} or null.
function plannedTime(gtiTime, offsetMin) {
  var d = gtiTime && /^(\d\d)\.(\d\d)\.(\d{4})$/.exec(gtiTime.date);
  var t = gtiTime && /^(\d\d):(\d\d)$/.exec(gtiTime.time);
  if (!d || !t) return null;
  // Hamburg local time, held in UTC fields so the phone's zone doesn't matter
  var local = Date.UTC(+d[3], +d[2] - 1, +d[1], +t[1], +t[2] + (offsetMin || 0));
  var offset = hamburgOffset(local - HOUR_MS);
  if (offset === '+0200') offset = hamburgOffset(local - 2 * HOUR_MS);
  var utc = local - (offset === '+0200' ? 2 : 1) * HOUR_MS;
  var l = new Date(local);
  var time = l.getUTCFullYear() + '-' + pad2(l.getUTCMonth() + 1) + '-' + pad2(l.getUTCDate()) +
    'T' + pad2(l.getUTCHours()) + ':' + pad2(l.getUTCMinutes()) + ':00.000' + offset;
  return { time: time, secs: utc / 1000 };
}

// GTI dateTime ('2026-10-07T00:17:00.000+0200') to Unix seconds, or null.
// Parsed by hand: Date.parse doesn't reliably accept '+0200'.
var GTI_TIME_RE = /^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d)(?::(\d\d))?(?:\.\d+)?([+-])(\d\d):?(\d\d)$/;

function parseGtiTime(str) {
  var m = typeof str === 'string' && GTI_TIME_RE.exec(str);
  if (!m) return null;
  var ms = Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +(m[6] || 0));
  var offsetMin = (+m[8] * 60 + +m[9]) * (m[7] === '-' ? -1 : 1);
  return ms / 1000 - offsetMin * 60;
}

function delayMinutes(seconds) {
  return typeof seconds === 'number' ? Math.round(seconds / 60) : 0;
}

function makeStop(station, time, delay, cancelled) {
  return {
    name: truncateUtf8((station && station.name) || '?', NAME_MAX_BYTES),
    id: station && station.id,
    time: parseGtiTime(time) || 0,
    delay: delayMinutes(delay),
    cancelled: !!cancelled,
  };
}

// Stops of a departureCourse response: the first element's fromStation,
// then every toStation. Each stop's time is when the trip leaves it
// (arrival for the last). focus is the user's stop: matched by station id
// (preferring the one at the planned time, as loop lines pass a stop twice),
// otherwise by planned time, otherwise 0. Returns null for an empty course.
function buildStops(resp, stationId, plannedSecs) {
  var els = resp && resp.courseElements;
  if (!els || !els.length) return null;

  var stops = [makeStop(els[0].fromStation, els[0].depTime, els[0].depDelay, els[0].fromCancelled)];
  for (var i = 0; i < els.length; i++) {
    var next = els[i + 1];
    stops.push(next ?
      makeStop(els[i].toStation, next.depTime, next.depDelay, els[i].toCancelled) :
      makeStop(els[i].toStation, els[i].arrTime, els[i].arrDelay, els[i].toCancelled));
  }

  var focus = -1;
  for (var j = 0; j < stops.length; j++) {
    var idMatch = stationId && stops[j].id === stationId;
    var timeMatch = stops[j].time === plannedSecs;
    if (idMatch && timeMatch) { focus = j; break; }
    if (idMatch && focus === -1) focus = j;
  }
  if (focus === -1) {
    for (var k = 0; k < stops.length; k++) {
      if (stops[k].time === plannedSecs) { focus = k; break; }
    }
  }
  return { stops: stops, focus: Math.max(focus, 0) };
}

// At most max stops, keeping up to STOPS_BEFORE_FOCUS before the user's stop
function windowStops(course, max) {
  var len = course.stops.length;
  if (len <= max) return course;
  var start = Math.max(0, Math.min(course.focus - STOPS_BEFORE_FOCUS, len - max));
  return { stops: course.stops.slice(start, start + max), focus: course.focus - start };
}

// Demo route for a demo departure: the user's station at index 4, the
// departure's direction last, 2 minutes between stops. The departure's
// delay applies from the user's stop on.
var DEMO_ROUTE_STOPS = [
  'Hauptbahnhof Süd', 'Mönckebergstraße', 'Rathaus', 'Rödingsmarkt', null,
  'Lübecker Straße', 'Uhlandstraße', 'Mundsburg', 'Hamburger Straße',
  'Dehnhaide', 'Barmbek', null,
];
var DEMO_FOCUS = 4;

function demoCourse(dep, station, nowSecs) {
  var focusTime = Math.floor(nowSecs / 60) * 60 + (dep.minutes || 0) * 60;
  var stops = [];
  for (var i = 0; i < DEMO_ROUTE_STOPS.length; i++) {
    var name = DEMO_ROUTE_STOPS[i] ||
      (i === DEMO_FOCUS ? station : dep.direction) || '?';
    stops.push({
      name: truncateUtf8(name, NAME_MAX_BYTES),
      time: focusTime + (i - DEMO_FOCUS) * 120,
      delay: i >= DEMO_FOCUS ? (dep.delay || 0) : 0,
      cancelled: false,
    });
  }
  return { stops: stops, focus: DEMO_FOCUS };
}

module.exports = {
  MAX_COURSE_STOPS: MAX_COURSE_STOPS,
  truncateUtf8: truncateUtf8,
  hamburgOffset: hamburgOffset,
  plannedTime: plannedTime,
  parseGtiTime: parseGtiTime,
  buildStops: buildStops,
  windowStops: windowStops,
  demoCourse: demoCourse,
};
