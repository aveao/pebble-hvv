module.exports = [
  {
    "type": "heading",
    "defaultValue": "HVV Departures"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Favorite Stops"
      },
      {
        "type": "input",
        "messageKey": "FAV_1",
        "label": "Favorite 1",
        "defaultValue": ""
      },
      {
        "type": "input",
        "messageKey": "FAV_2",
        "label": "Favorite 2",
        "defaultValue": ""
      },
      {
        "type": "input",
        "messageKey": "FAV_3",
        "label": "Favorite 3",
        "defaultValue": ""
      },
      {
        "type": "input",
        "messageKey": "FAV_4",
        "label": "Favorite 4",
        "defaultValue": ""
      },
      {
        "type": "input",
        "messageKey": "FAV_5",
        "label": "Favorite 5",
        "defaultValue": ""
      },
      {
        "type": "text",
        "defaultValue": "Enter station names as shown on HVV.de. Leave blank to skip."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Bring your own GTI key (advanced)"
      },
      {
        "type": "input",
        "messageKey": "CONFIG_USER",
        "label": "GTI Username",
        "defaultValue": ""
      },
      {
        "type": "input",
        "messageKey": "CONFIG_PASSWORD",
        "label": "GTI Password",
        "defaultValue": "",
        "attributes": {
          "type": "password"
        }
      },
      {
        "type": "text",
        "defaultValue": "Most users should leave these blank, the app uses a shared service by default. Power users with their own GTI account can enter credentials here to bypass the shared service. Get credentials from <a href='https://www.hvv.de/de/fahrplaene/abruf-fahrplaninfos/datenabruf'>HVV</a> (geofox API)."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Display"
      },
      {
        "type": "input",
        "messageKey": "CONFIG_MAX_NEARBY",
        "label": "Nearby stations to display",
        "defaultValue": "3",
        "attributes": {
          "type": "number",
          "min": 1,
          "max": 10
        }
      },
      {
        "type": "input",
        "messageKey": "CONFIG_MAX_DEPARTURES",
        "label": "Max departures to display",
        "defaultValue": "10",
        "attributes": {
          "type": "number",
          "min": 10,
          "max": 30
        }
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Privacy"
      },
      {
        "type": "text",
        "defaultValue": "By default the app sends limited data (watch serial, IP, station name or coordinates) to a shared service that proxies HVV requests. Read the full <a href='https://github.com/aveao/pebble-hvv/blob/main/PRIVACY.md'>privacy policy</a> for what is collected, what is logged, and your GDPR rights."
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
