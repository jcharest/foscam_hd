#ifndef FOSCAM_DEVICE_H
#define FOSCAM_DEVICE_H

#include <QIODevice>
#include <foscam.h>

namespace Ui {
class FoscamDevice;
}

class FoscamDevice : public QIODevice
{
    Q_OBJECT
public:
	FoscamDevice(Foscam & in_Foscam, QObject * in_pParent = nullptr);
    virtual bool open(OpenMode in_Mode) override;
    virtual void close() override;
    virtual bool isSequential() const override;
    virtual qint64 bytesAvailable() const override;


    virtual bool	atEnd () const {
    	return false;
    }
    virtual qint64	bytesToWrite () const
    {
    	return 0;
    }
    virtual bool	canReadLine () const
    {
    	return true;
    }

    virtual qint64	pos () const
    {
    	return 0;
    }

    virtual bool	reset ()
    {
    	return true;
    }
    virtual bool	seek ( qint64 pos )
    {
    	return true;
    }

    virtual qint64	size () const
    {
    	return 0;
    }

    virtual bool	waitForBytesWritten ( int msecs )
    {
    	return false;
    }
    virtual bool	waitForReadyRead ( int msecs )
    {
    	return false;
    }


protected:
    virtual qint64 readData(char * in_pData, qint64 in_un64MaxSize) override;
    virtual qint64 writeData(const char * in_pData, qint64 in_un64MaxSize) override;
private:

    Foscam & mCam;
    Q_DISABLE_COPY(FoscamDevice)
};

#endif // FOSCAM_DEVICE_H
