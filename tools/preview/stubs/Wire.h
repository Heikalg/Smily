// Host stub of Wire with a simulated MPU behind it, so the shake detector
// can be exercised without hardware.
#pragma once

#include <cstdint>
#include <cstring>

struct HostMpuState {
  bool present = true;
  uint8_t address = 0x68;
  uint8_t whoami = 0x68;
  int16_t ax = 0, ay = 0, az = 4096;  // 4096 LSB == 1 g at the +-8 g setting
  int16_t gx = 0, gy = 0, gz = 0;
  bool asleep = false;
  int reads = 0;
};
extern HostMpuState hostMpu;

class HostTwoWire {
 public:
  bool begin(int, int, uint32_t) { return true; }
  bool begin() { return true; }
  void setClock(uint32_t) {}

  void beginTransmission(uint8_t addr) {
    _addr = addr;
    _nwritten = 0;
  }
  size_t write(uint8_t b) {
    if (_nwritten < sizeof(_written)) _written[_nwritten++] = b;
    return 1;
  }
  uint8_t endTransmission(bool /*stop*/ = true) {
    if (!deviceHere()) return 2;  // NACK on address
    if (_nwritten >= 1) _reg = _written[0];
    if (_nwritten >= 2 && _reg == 0x6B) hostMpu.asleep = (_written[1] & 0x40) != 0;
    return 0;
  }

  int requestFrom(int addr, int n) {
    _addr = (uint8_t)addr;
    if (!deviceHere()) return 0;
    _nread = 0;
    _navail = 0;
    if (_reg == 0x75) {
      _rx[_navail++] = hostMpu.whoami;
    } else if (_reg == 0x3B) {
      hostMpu.reads++;
      const int16_t v[7] = {hostMpu.ax, hostMpu.ay, hostMpu.az, 0,
                            hostMpu.gx, hostMpu.gy, hostMpu.gz};
      for (int i = 0; i < 7 && _navail + 1 < (int)sizeof(_rx); i++) {
        _rx[_navail++] = (uint8_t)(v[i] >> 8);
        _rx[_navail++] = (uint8_t)(v[i] & 0xFF);
      }
    }
    if (n > _navail) return _navail;  // short read -> driver treats as failure
    _navail = n;
    return n;
  }
  int available() { return _navail - _nread; }
  int read() { return (_nread < _navail) ? _rx[_nread++] : -1; }

 private:
  bool deviceHere() const {
    if (_addr == 0x3C) return true;  // the OLED always answers
    return hostMpu.present && _addr == hostMpu.address;
  }
  uint8_t _addr = 0;
  uint8_t _reg = 0;
  uint8_t _written[8] = {0};
  size_t _nwritten = 0;
  uint8_t _rx[20] = {0};
  int _navail = 0, _nread = 0;
};

extern HostTwoWire Wire;
