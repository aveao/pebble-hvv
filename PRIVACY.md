# Privacy Policy

**Effective:** 2026-04-26
**Applies to:** the *pebble-hvv* watch app and its accompanying API proxy at `pebble-hvv-api.ave.zone`.

This document explains, in plain language, what personal data the service handles, why, where it goes, and what your rights are under the EU General Data Protection Regulation (GDPR).

## 1. Who is responsible

The data controller for the proxy service is the maintainer of *pebble-hvv*:

- **Contact:** pebble-hvv-api [at] ave [dot] zone

If you use the app in **bring-your-own-credentials mode** or **demo mode** (see §3), the controller responsibilities are different, see those sections.

## 2. The three operating modes

The app runs in one of three modes, picked automatically:

| Mode | When | Data leaves your phone? |
|------|------|---|
| **Demo** | No HVV credentials configured and no proxy build available | No data leaves your phone. Pre-canned demo departures are shown. |
| **Proxy** *(default for the published `.pbw`)* | No HVV credentials, but the build was configured with the proxy settings | Requests go to `pebble-hvv-api.ave.zone` and are forwarded to HVV under the maintainer's API key. |
| **Bring-your-own** | You entered your own HVV/GTI credentials in the configuration page | Requests go directly from your phone to `gti.geofox.de`. The proxy is **not** involved; the maintainer never sees your data. |

The rest of this policy mainly concerns **Proxy mode**, since that is the only mode in which the maintainer processes any of your data.

## 3. What data is processed

### 3.1 On your phone (all modes)

The PebbleKit JS environment on your phone holds:

- Your favorite station names (entered in the app's configuration page)
- Display preferences (number of nearby stations / departures to show)
- Your HVV credentials, **only** if you entered them in BYO mode
- The latest timetable response, kept briefly so the watch face can render it

This data stays on your phone. It is not transmitted anywhere except to fulfil requests as described below. Uninstalling the app removes it.

### 3.2 Sent to the proxy (Proxy mode only)

When the app makes a request in proxy mode, the following are transmitted to `pebble-hvv-api.ave.zone`:

- Your **watch serial number** (a 12-character alphanumeric string baked into the watch hardware), sent in the `X-Watch-Serial` request header. Used for abuse prevention.
- For station search: your **approximate GPS coordinates** at the moment you opened the app, plus a search radius and station-type filter.
- For departure lookup: the **name of the station you selected**, plus the requested time window.
- Your **IP address** (received automatically by Cloudflare as the network endpoint of your request).

### 3.3 Sent to hvv Hamburger Verkehrsverbund Gesellschaft mbH / gti.geofox.de (HBT Hamburger Berater Team GmbH)

The API server reconstructs each request, signed with the maintainer's HVV API credentials, to `gti.geofox.de` (HVV's public timetable API). HVV therefore receives:

- The same coordinates / station name / time fields described in §3.2.
- The maintainer's API credentials (not yours).
- The maintainer's server IP (not yours).

HVV is an independent controller for any data they retain. Their terms of service govern that retention, which can be viewed here: https://www.hvv.de/de/fahrplaene/abruf-fahrplaninfos/datenabruf

### 3.4 Stored on the proxy

For each request, the proxy writes **exactly one** structured log line containing:

- Timestamp
- Watch serial number
- IP address (as seen by Cloudflare)
- Endpoint name
- HTTP response status code
- HVV API response status code
- Request duration in milliseconds

The proxy **does not** log the request body, the response body, or any field derived from them. In particular: GPS coordinates and station names **are not stored** in the proxy's logs. They pass through in transit and are then dropped.

These logs are retained in **Cloudflare Workers Observability** for as long as Cloudflare's free tier retains them (currently 3 days). Cloudflare's edge logs may also include IP and request metadata under their own retention; see <https://www.cloudflare.com/privacypolicy/>.

No KV store, database, or external log destination is currently used.

## 4. Why we process this data (legal basis)

Processing is based on **Article 6(1)(f) GDPR - legitimate interest**: providing the timetable look-up service that you installed, and protecting it from abuse.

The legitimate-interest balancing test:

- The data processed is the minimum needed (a header for rate limiting, plus what HVV requires to answer the query).
- No profile, account, or persistent identifier is created beyond what already lives on your watch.
- The risk to you is low: the only stored fields are abuse-investigation metadata.
- The benefit to you is direct: you get real-time HVV departures without registering for an HVV API key.

If you'd rather not have your watch serial or IP processed by the proxy, you can switch to **BYO mode** (enter your own HVV credentials) or **demo mode** (clear them and rebuild without proxy config), see §2.

## 5. Who else gets the data

The service relies on two external processors:

- **Cloudflare, Inc.**: runs the proxy code at the network edge and is the only party that sees your request before it reaches HVV. Privacy policy: <https://www.cloudflare.com/privacypolicy/>.
- **Hamburger Verkehrsverbund GmbH (HVV)** and **geofox (HBT Hamburger Berater Team GmbH)**: receives the proxied request bodies (coordinates / station name / time) in order to answer the query. Privacy information: <https://www.hvv.de/en/datenschutz>.

No data is shared with anyone else. There is no advertising, analytics, or third-party tracking.

## 6. International transfers

Cloudflare operates a global network, so a request may be terminated at an edge location inside or outside the European Economic Area depending on your network's routing. Cloudflare's transfer safeguards (Standard Contractual Clauses, etc.) apply to any onward transfer. HVV is located in Germany.

## 7. Retention

| Data | Where | Retention |
|------|-------|---|
| App settings / favorites | On your phone | Until you remove the app |
| Latest timetable response | On your phone | Until refreshed (≤30 s) |
| Proxy log line (metadata) | Cloudflare Workers Logs | Per Cloudflare's free-tier retention (currently up to 3 days) |
| Cloudflare edge access logs | Cloudflare | Per Cloudflare's terms |
| HVV-side records | HVV | Per HVV's terms |

The maintainer does not run any database or persistent log store of their own.

## 8. Your rights

Under the GDPR you have the following rights with respect to data the maintainer processes (i.e. what's in §3.4):

- **Access:** you can ask for a copy of any log lines that mention your watch serial or IP.
- **Erasure:** you can ask for log lines associated with your watch serial or IP to be deleted before their normal expiry.
- **Restriction:** you can ask for processing of those entries to be paused.
- **Objection:** you can object to processing based on legitimate interest at any time. The maintainer will then stop processing unless there are overriding grounds.
- **Portability:** you can ask for a machine-readable copy of those entries.

To exercise any of these rights, email **pebble-hvv-api [at] ave [dot] zone** with your watch serial number (printed on the back of your watch) and a description of what you want. Expect a reply within 30 days.

You can also lodge a complaint with a supervisory authority. For users in Hamburg, that is the *Hamburgischer Beauftragter für Datenschutz und Informationsfreiheit* (<https://datenschutz-hamburg.de>). Users elsewhere in the EU can contact their national supervisory authority.

## 9. Changes to this policy

Material changes will be reflected in this file in the project's git history.
