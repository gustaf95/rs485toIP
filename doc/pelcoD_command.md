# Pelco-D Command List

RS485 PTZ 컨트롤러 기준 Pelco-D 커맨드 셋. Address(Byte2)는 예시로 `01`을 사용.

| No  | Command                | Byte1 | Byte2 | Byte3 | Byte4 | Byte5                  | Byte6                  | Byte7 |
| --- | ----------------------- | ----- | ----- | ----- | ----- | ----------------------- | ----------------------- | ----- |
| 1   | Pan Left                | FF    | 01    | 00    | 04    | DATA1                   | DATA2                   | check |
| 2   | Pan Right               | FF    | 01    | 00    | 02    | DATA1                   | DATA2                   | check |
| 3   | Tilt Up                 | FF    | 01    | 00    | 08    | DATA1                   | DATA2                   | check |
| 4   | Tilt Down               | FF    | 01    | 00    | 10    | DATA1                   | DATA2                   | check |
| 5   | Zoom Tele               | FF    | 01    | 00    | 20    | 00                      | 00                      | check |
| 6   | Zoom Wide               | FF    | 01    | 00    | 40    | 00                      | 00                      | check |
| 7   | Focus Near              | FF    | 01    | 01    | 00    | 00                      | 00                      | check |
| 8   | Focus Far               | FF    | 01    | 00    | 80    | 00                      | 00                      | check |
| 9   | Set Preset              | FF    | 01    | 00    | 03    | 00                      | Preset ID                | check |
| 10  | Goto Preset             | FF    | 01    | 00    | 07    | 00                      | Preset ID                | check |
| 11  | Clear Preset            | FF    | 01    | 00    | 05    | 00                      | Preset ID                | check |
| 12  | Run Group               | FF    | 01    | 00    | 23    | 00                      | Zone ID                  | check |
| 13  | Run Swing               | FF    | 01    | 00    | 1B    | 00                      | 00                      | check |
| 14  | Initialize Pan/Tilt     | FF    | 01    | 00    | 0F    | 00                      | 00                      | check |
| 15  | Aux On                  | FF    | 01    | 00    | 09    | 00                      | 01 ~ 06                  | check |
| 16  | Aux Off                 | FF    | 01    | 00    | 0B    | 00                      | 01 ~ 06                  | check |
| 17  | Set Pan Position        | FF    | 01    | 00    | 4B    | Msb Of Pan Position      | Lsb Of Pan Position      | check |
| 18  | Set Tilt Position       | FF    | 01    | 00    | 4D    | Msb Of Tilt Position     | Lsb Of Tilt Position     | check |
| 19  | Set Zoom Position       | FF    | 01    | 00    | 4F    | Msb Of Zoom Position     | Lsb Of Zoom Position     | check |
| 20  | Set Focus Position      | FF    | 01    | 00    | 5F    | Msb Of Focus Position    | Lsb Of Focus Position    | check |
| 21  | Query Pan Position      | FF    | 01    | 00    | 51    | 00                      | 00                      | check |
| 22  | Query Tilt Position     | FF    | 01    | 00    | 53    | 00                      | 00                      | check |
| 23  | Query Zoom Position     | FF    | 01    | 00    | 55    | 00                      | 00                      | check |
| 24  | Query Focus Position    | FF    | 01    | 00    | 61    | 00                      | 00                      | check |
| 25  | Response Pan Position   | FF    | 01    | 00    | 59    | Msb Of Pan Position      | Lsb Of Pan Position      | check |
| 26  | Response Tilt Position  | FF    | 01    | 00    | 5B    | Msb Of Tilt Position     | Lsb Of Tilt Position     | check |
| 27  | Response Zoom Position  | FF    | 01    | 00    | 6D    | Msb Of Zoom Position     | Lsb Of Zoom Position     | check |
| 28  | Response Focus Position | FF    | 01    | 00    | 63    | Msb Of Focus Position    | Lsb Of Focus Position    | check |

- DATA1 = Pan Speed, DATA2 = Tilt Speed (Pan/Tilt 이동 커맨드 한정)
- check = Checksum (Byte2~Byte6 합산)
