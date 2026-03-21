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
        "defaultValue": "Station"
      },
      {
        "type": "input",
        "messageKey": "CONFIG_STATION",
        "label": "Station Name",
        "defaultValue": "Jungfernstieg",
        "description": "Enter the station name as shown on HVV.de"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "API Credentials"
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
        "defaultValue": "Leave blank to use demo data. Get API credentials from <a href='https://gti.geofox.de/'>gti.geofox.de</a>."
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
