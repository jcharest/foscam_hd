#include <iostream>
#include <thread>
#include <foscam.h>
#include <web_app.h>


using namespace std;

int main(int argc, char *argv[])
{
  boost::asio::io_service io_service;
  shared_ptr<Foscam> cam;
  try
  {
    cam = make_shared<Foscam>(io_service, "192.168.1.8", 88, time(NULL), "admin","***REMOVED***", 30);
    cam->Connect();
  }
	catch (exception & ex)
  {
	  cerr << "Failed to connect to camera: " << ex.what() << endl;
  }

	// Start asio thread
  thread io_thread([&io_service](){
    try
    {
      io_service.run();
    }
    catch (exception & ex)
    {
      cerr << "Failure occured while running service thread: " << ex.what() << endl;
    }
  });

  try
  {
    WebApp App(cam);
    getchar();
    cam->Disconnect();
    io_thread.join();
  }
  catch (exception & ex)
  {
    cerr << "Failure occured while running web application: " << ex.what() << endl;
  }

	return EXIT_SUCCESS;
}
