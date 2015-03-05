#include <ctime>
#include "main_window.h"
#include "ui_main_window.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
	mMediaPlayer(this, QMediaPlayer::StreamPlayback),
    mCam("hugcam", 88, time(NULL), "admin","***REMOVED***"),
	mDev(mCam)
{
    ui->setupUi(this);

    mDev.open(QIODevice::ReadOnly);
    mMediaPlayer.setMedia(0, &mDev);
    mMediaPlayer.setVideoOutput(ui->VideoViewer);
    ui->VideoViewer->show();
    mMediaPlayer.play();

    handleButton();

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
