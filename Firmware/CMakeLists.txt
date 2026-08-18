cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(macropad)

target_sources(app PRIVATE
  src/main.c
  src/app_events.c
  src/app_sm.c
  src/hog.c
  )
