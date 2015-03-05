#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <foscam_device.h>
#include <foscam.h>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
	void handleButton();

private:
    Ui::MainWindow *ui;
    QMediaPlayer mMediaPlayer;
    Foscam mCam;
    FoscamDevice mDev;
};

#endif // MAIN_WINDOW_H
