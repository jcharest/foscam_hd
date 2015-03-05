#ifndef FOSCAM_HD_IP_CAM_H
#define FOSCAM_HD_IP_CAM_H



class FoscamHdIpCam : public IPCam
{
public:
    FoscamHdIpCam(const std::string & in_strIPAddress);

    virtual bool Login(const std::string & in_strUser, const std::string & in_strPassword);
private:

};

#endif // FOSCAM_HD_IP_CAM_H
