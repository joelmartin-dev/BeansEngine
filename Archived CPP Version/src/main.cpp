#include <iostream>
#include <exception>
#include <filesystem>

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

  app.measurement_file_name = 
    (std::filesystem::path("Measuring") / std::filesystem::path(argv[0]).stem()).string().append(".csv");

  try
  {
    app.Run();
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  main(__argc, __argv);
  return 0;
}
#endif
