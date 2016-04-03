#include <iostream>

#include "fossdk.h"

namespace {

  const unsigned int TIMEOUT = 1000;

}  // namespace

int main(int argc, char * argv[]) {

  auto ret = FosSdk_Init();
  if(ret != 1) {
    std::cerr << "Failed to init sdk: " << ret << std::endl;
    return EXIT_FAILURE;
  }

  auto cam = FosSdk_Create(
      const_cast<char *>("192.168.1.8"),
      const_cast<char *>(std::to_string(time(nullptr)).c_str()),
      const_cast<char *>("hugcam"), const_cast<char *>("lkpdfq46M"), 88, 88,
      FOSIPC_H264, FOSCNTYPE_IP);
  if(!cam) {
    std::cerr << "Failed to create cam" << std::endl;

    FosSdk_DeInit();
    return EXIT_FAILURE;
  }

  int user_privilege;
  auto res = FosSdk_Login(cam, &user_privilege, TIMEOUT);
  if(res != FOSCMDRET_OK) {
    std::cerr << "Failed to logon to cam: " << std::hex << res << std::endl;

    FosSdk_Release(cam);
    FosSdk_DeInit();
    return EXIT_FAILURE;
  }

  res = FosSdk_OpenVideo(cam, FOSSTREAM_MAIN, TIMEOUT);
  if(res != FOSCMDRET_OK) {
    std::cerr << "Failed to open video: " << std::hex << res << std::endl;

    FosSdk_Logout(cam, TIMEOUT);
    FosSdk_Release(cam);
    FosSdk_DeInit();
    return EXIT_FAILURE;
  }

  FosSdk_CloseVideo(cam, TIMEOUT);
  FosSdk_Logout(cam, TIMEOUT);
  FosSdk_Release(cam);
  FosSdk_DeInit();

  return EXIT_SUCCESS;
}
