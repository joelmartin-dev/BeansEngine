#include <cstdlib>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#include <Windows.h>
#endif

#include "App.hpp"

int main(int argc, char** argv)
{
  App app = {};
#ifndef _WIN32
  if (argc > 1) app.useWayland = false;
  else app.useWayland = true;
#endif

  try
  {
    app.Run();
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    main(0, "");
    return 0;
}
#endif
