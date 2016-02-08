#include <ctime>
#include <iostream>
#include "main_window.h"
#include "ui_main_window.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
	mMediaPlayer(this, QMediaPlayer::Flags(QMediaPlayer::LowLatency) | QMediaPlayer::Flags(QMediaPlayer::StreamPlayback)),
    //mCam("hugcam", 88, time(NULL), "admin","***REMOVED***"),
	mCam("192.168.1.8", 88, time(NULL), "admin","***REMOVED***", 30)
	//mDev(mCam)
{
    ui->setupUi(this);

    mCam.VideoOn();
    mCam.AudioOn();
    //mDev.open(QIODevice::ReadOnly);
    //mMediaPlayer.setMedia(0, &mDev);

    QMediaResource Res(QUrl::fromLocalFile("/home/jonathan/hug_cam/video_file"), "video/H264");
    std::cout << Res.videoCodec().toStdString() << std::endl;
    std::cout << Res.sampleRate() << std::endl;
    std::cout << Res.channelCount() << std::endl;
    std::cout << Res.videoBitRate() << std::endl;
    std::cout << Res.resolution().height() << std::endl;

    QMediaContent Content(Res);

    mMediaPlayer.setMedia(Content);
    mMediaPlayer.setVideoOutput(ui->VideoViewer);
    ui->VideoViewer->show();

    mMediaPlayer.play();

    //handleButton();

    connect(ui->PlayButton, SIGNAL(released()), this, SLOT(handleButton()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::handleButton()
{
    qDebug() << mMediaPlayer.mediaStatus() << mMediaPlayer.availableMetaData();
    qDebug() << mMediaPlayer.state() << mMediaPlayer.errorString();
    qDebug() << mMediaPlayer.bufferStatus();
    qDebug() << mMediaPlayer.media().canonicalResource().dataSize() << mMediaPlayer.media().canonicalResource().videoCodec();
}
