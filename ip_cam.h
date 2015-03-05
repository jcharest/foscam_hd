#ifndef IP_CAM_H
#define IP_CAM_H

class IPCam
{
public:
    virtual ~IPCam() = default;

    virtual bool VideoOn() = 0;
    virtual bool AudioOn() = 0;
};

#endif // IP_CAM_H
