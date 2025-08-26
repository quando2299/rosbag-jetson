// Project: Robot simulator
// Copyright (c) 2025 M2M craft Co., Ltd.

// String mockDataRobot(int timestamp) => '''
// {
//   "state": {
//     "reported": {
//       "instruction": {
//         "mTmMsc": $timestamp,
//         "opCd": 0,
//         "opCdDetail": 0,
//         "status": 2
//       },
//       "position": {
//         "mTmMsc": $timestamp,
//         "latd": 36.089515979,
//         "lond": 139.537211528,
//         "extra": {
//           "hdng": 30.1,
//           "alt": 87.12345678901,
//           "yaw": -57.29577951308,
//           "ptch": 3.12345678901,
//           "roll": 0.00821234567
//         }
//       },
//       "status": {
//         "mTmMsc": $timestamp,
//         "eSpd": 1198,
//         "vSpd": 4.3,
//         "sSpd": 5,
//         "shttl": 1,
//         "htch": 1,
//         "string": 513,
//         "emrgncy": 0,
//         "wrk": 1,
//         "engn": 1,
//         "ipadd": "soemon-cho.miemasu.net:63104",
//         "port1": "63104",
//         "port2": "8888",
//         "usr": "user001",
//         "pass": "password",
//         "path": "/nphMotionJpeg?Resolution=640x480&Quality=Motion",
//         "thumbPath": "/nphMotionJpeg?Resolution=320x240&Quality=Motion",
//         "mapVer" : "001"
//       },
//       "rbtMkr": "0001",
//       "rbtSN": "1234567"
//     }
//   }
// }''';

String mockTCLogData() => '''
{
  "seqCounter": 0,
  "areaId": "E97C315E-4D4D-4783-B7C9-1CAFD28DB1C6",
  "fieldId": "731B2AF2-E37E-4287-82F5-26294E3EFDB7",
  "NAME": "0011223344556677",
  "timeLog": {
    "timeLogHeader": "PFRJTSBBPSIiIEQ9IjQiPgogICAgPFBUTiBBPSIiIEI9IiIgRD0iIi8+CiAgICA8RExWIEE9IjA4MTUiIEI9IiIgQz0iREVUMSIvPgogICAgPERMViBBPSI0NzExIiBCPSIiIEM9IkRFVDIiLz4KPC9USU0+",
    "checkSum": 1,
    "dataSets": [
      {
        "dataCounter": 0,
        "data": "AQAAAAEAAQAAAP////8BAgAKAAAAAQ8AAAA="
      },
      {
        "dataCounter": 1,
        "data": "ZQAAAAEAAfQAAAH0AAABAQEPAAAA"
      }
    ]
  },
  "time": [
    {
      "startTimeOfDay": 3600000,
      "startDate": 16071,
      "stopTimeOfDay": 3500000,
      "stopDate": 16071,
      "position": {
        "positionNorth": 12,
        "positionEast": 21,
        "positionUp": 12,
        "positionStatus": 0,
        "pdop": 45,
        "hdop": 31,
        "numberOfSatellites": 8,
        "gpsUtcTime": 3600000,
        "gpsUtcData": 16071
      },
       "dlv": {
          "processDataDDI": 1,
          "processDataValue": 100
       }
    }
  ]
}''';

String mockUpdateDocument(int timestamp) {
  return '''{
    "gwDvc": 0,
    "opeDvc": 0,
    "operator": 0,
    "rbtMkr": "0001",
    "rbtSN": "1111111",
    "imple": ["1234567"],
    "mTmGw": $timestamp,
    "instruction": {
      "mTmMsc": $timestamp,
      "mode": 1,
      "opCd": 1,
      "planId": null,
      "areaId": null,
      "routeId": null,
      "poiId": null,
      "latd": 36.089515979,
      "lond": 139.537211528,
      "taskId": 0,
      "GPS position": {
        "mTmMsc": $timestamp,
        "latd": 36.089515979,
        "lond": 139.537211528,
        "alt": 21.537211528,
        "HDOP": 0,
        "VDOP": 0,
        "quality": 0,
        "extra": {
          "hdng": 30.1,
          "yaw": -57.29577951308,
          "ptch": 3.12345678901,
          "roll": 0.00821234567
        }
      },
      "SLAM position": {
        "status": 0,
        "latd": 36.089515979,
        "lond": 139.537211528,
        "quality": 10
      }
    },
    "tractionStatus": {
      "operator": 0,
      "mode": 1,
      "opSts": 1,
      "engn": 1,
      "eSpd": 0,
      "shttl": 0,
      "vSpd": 0,
      "sSpd": 0,
      "string": 0,
      "trnsPos": 0,
      "htch": {
        "autMd": 0,
        "position": 0,
        "state": 0,
        "draft": 0,
        "force": 0,
        "limit": 0,
        "exitCd": 0
      },
      "pto": {
        "autMd": 0,
        "mode": 0,
        "speed": 0,
        "setpoint": 0,
        "limit": 0,
        "exitCd": 0
      },
      "auxVlv": [
        {
          "vlvNo": 0,
          "autMd": 0,
          "extFlow": 0,
          "retFlow": 0,
          "state": 0
        }
      ]
    },
    "perception": [
      {
        "unitNo": 0,
        "state": 0
      }
    ],
    "obstcl": [
      {
        "dtctUnit": 0,
        "kind": 0,
        "dist": 0,
        "yaw": 0,
        "pitch": 0,
        "width": 0,
        "height": 0
      }
    ],
    "autoUnitSts": {},
    "guidance": {
      "mode": 0,
      "status": 0,
      "exitCd": 0
    },
    "detourWaypoint": [
      {
        "latd": 36.089515979,
        "lond": 139.537211528
      }
    ],
    "wrkInfList": {
      "dvcNo": 0,
      "elmNo": 0,
      "paramNo": 0,
      "value": 0
    },
    "failureList": {
      "dvcNo": 0,
      "elmNo": 0,
      "paramNo": 0,
      "failMd": 0,
      "count": 0
    }
  }''';
}
