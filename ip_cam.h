#ifndef IP_CAM_H
#define IP_CAM_H

class IPCam
{
public:
    virtual ~IPCam() = default;

    virtual void Connect() = 0;
    virtual void Disconnect() = 0;
    virtual bool VideoOn() = 0;
    virtual bool AudioOn() = 0;
    virtual unsigned int GetAvailableVideoStreamData() = 0;
    virtual unsigned int GetVideoStreamData(uint8_t * in_pData, unsigned int in_unDataLength) = 0;
};

#endif // IP_CAM_H
