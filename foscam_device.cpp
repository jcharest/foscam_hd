#include "foscam_device.h"

using namespace std;


FoscamDevice::FoscamDevice(Foscam & in_Foscam, QObject * in_pParent) : QIODevice(in_pParent), mCam(in_Foscam)
{
}

bool FoscamDevice::open(OpenMode in_Mode)
{
	try
	{
		mCam.VideoOn();
		mCam.AudioOn();
	}
	catch(FoscamException & Ex)
	{
		return false;
	}

	return QIODevice::open(in_Mode);
}

void FoscamDevice::close()
{
	QIODevice::close();
}

bool FoscamDevice::isSequential() const
{
	return true;
}

qint64 FoscamDevice::bytesAvailable() const
{
	return mCam.GetVideoDataAvailable() + QIODevice::bytesAvailable();
}

qint64 FoscamDevice::readData(char* in_pData, qint64 in_un64MaxSize)
{
	return mCam.GetVideoData(reinterpret_cast<uint8_t *>(in_pData), in_un64MaxSize);
}

qint64 FoscamDevice::writeData(const char * in_pData, qint64 in_un64MaxSize)
{
	return -1;
}
