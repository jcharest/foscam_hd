#include <iostream>
#include <thread>

#include "foscam.h"
#include "web_app.h"

int main(int argc, char * argv[]) {
  boost::asio::io_service io_service;
  std::shared_ptr<foscam_hd::Foscam> cam;
  try {
    cam = std::make_shared<foscam_hd::Foscam>(
        "192.168.1.8", 88, time(NULL), "hugcam", "***REMOVED***", io_service);
    cam->Connect();
  } catch (std::exception & ex) {
    std::cerr << "Failed to connect to camera: " << ex.what() << std::endl;
  }

  // Start asio thread
  std::thread io_thread(
      [&io_service]() {
        try {
          io_service.run();
        }
        catch (std::exception & ex) {
          std::cerr << "Failure occured while running service thread: "
                    << ex.what() << std::endl;
        }
      });

  try {
    foscam_hd::WebApp App(cam);
    getchar();
    cam->Disconnect();
    io_thread.join();
  } catch (std::exception & ex) {
    std::cerr << "Failure occured while running web application: " << ex.what()
              << std::endl;
  }

  return EXIT_SUCCESS;
}
