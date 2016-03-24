#ifndef IP_CAM_INTERFACE_H_
#define IP_CAM_INTERFACE_H_

#include <cstdint>

namespace foscam_hd {

class IPCamInterface {
 public:
  virtual ~IPCamInterface() = default;

  virtual void Connect() = 0;
  virtual void Disconnect() = 0;
  virtual bool VideoOn() = 0;
  virtual bool AudioOn() = 0;
  virtual unsigned int GetAvailableVideoStreamData() = 0;
  virtual unsigned int GetVideoStreamData(uint8_t * in_pData,
                                          unsigned int in_unDataLength) = 0;
};

}  // namespace foscam_hd

#endif  // IP_CAM_INTERFACE_H_
