#!/usr/bin/env python
# 
# This takes a raw memory dump from the VGA video memory and displays it as an image
#
import sys
import os
from ctypes import *
from sdl2 import *

def setupPalette(surface):
    palette = surface.contents.format.contents.palette
    for i in range(0, 256):
        c = SDL_Color(default_palette[i*3], default_palette[(i*3)+1], default_palette[(i*3)+2], 255)
        SDL_SetPaletteColors(palette, byref(c), i, 1 );

def error(msg):
    print(msg)
    sys.exit(1)

def loadVgaData(file):
    if not os.path.isfile(file):
        error(f"File does not exist: {file}")
    readsize = 320*200;
    if (filesize := os.path.getsize(file)) != readsize:
        error(f"Unexpected file size: {filesize}")

    with open(file,'rb') as f:
        return list(f.read(readsize))

def refreshWindow(window, source):
    dest = SDL_GetWindowSurface(window)
    SDL_BlitScaled(source, None, dest, None)
    SDL_UpdateWindowSurface(window)

def main(file):
    SDL_Init(SDL_INIT_VIDEO)
    # create and setup window
    window = SDL_CreateWindow(b"VGA display",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE)

    # create and fill 8bpp image
    image = SDL_CreateRGBSurface(0,320,200,8,0,0,0,0)
    setupPalette(image)
    pixels = c_void_p(image.contents.pixels)
    pc=cast(pixels, POINTER(c_char))
    data = loadVgaData(file)
    for i in range(0, 320*200):
        pc[i]=data[i]

    # blit the image to a 32bpp surface that's compatible with the window surface for scaled blitting
    image32 = SDL_CreateRGBSurface(0,320,200,32,0xff,0xff00,0xff0000,0xff000000)
    SDL_BlitSurface(image, None, image32, None)

    # blit the 32bpp surface to the window with scaling to stretch the entire window
    refreshWindow(window, image32)

    running = True
    event = SDL_Event()
    while running:
        while SDL_PollEvent(byref(event)) != 0:
            if event.type == SDL_KEYDOWN or event.type == SDL_QUIT:
                running = False
                break
            if event.type == SDL_WINDOWEVENT and event.window.event == SDL_WINDOWEVENT_RESIZED:
                # window surface gets invalidated after rescaling
                refreshWindow(window, image32)

    SDL_FreeSurface(image)
    SDL_DestroyWindow(window)
    SDL_Quit()

default_palette = [ 0,   0,   0,    0,   0, 170,    0, 170,   0,    0, 170, 170,
170,   0,   0,  170,   0, 170,  170,  85,   0,  170, 170, 170,
 85,  85,  85,   85,  85, 255,   85, 255,  85,   85, 255, 255,
255,  85,  85,  255,  85, 255,  255, 255,  85,  255, 255, 255,
  0,   0,   0,   20,  20,  20,   32,  32,  32,   44,  44,  44,
 56,  56,  56,   69,  69,  69,   81,  81,  81,   97,  97,  97,
113, 113, 113,  130, 130, 130,  146, 146, 146,  162, 162, 162,
182, 182, 182,  203, 203, 203,  227, 227, 227,  255, 255, 255,
  0,   0, 255,   65,   0, 255,  125,   0, 255,  190,   0, 255,
255,   0, 255,  255,   0, 190,  255,   0, 125,  255,   0,  65,
255,   0,   0,  255,  65,   0,  255, 125,   0,  255, 190,   0,
255, 255,   0,  190, 255,   0,  125, 255,   0,   65, 255,   0,
  0, 255,   0,    0, 255,  65,    0, 255, 125,    0, 255, 190,
  0, 255, 255,    0, 190, 255,    0, 125, 255,    0,  65, 255,
125, 125, 255,  158, 125, 255,  190, 125, 255,  223, 125, 255,
255, 125, 255,  255, 125, 223,  255, 125, 190,  255, 125, 158,
255, 125, 125,  255, 158, 125,  255, 190, 125,  255, 223, 125,
255, 255, 125,  223, 255, 125,  190, 255, 125,  158, 255, 125,
125, 255, 125,  125, 255, 158,  125, 255, 190,  125, 255, 223,
125, 255, 255,  125, 223, 255,  125, 190, 255,  125, 158, 255,
182, 182, 255,  199, 182, 255,  219, 182, 255,  235, 182, 255,
255, 182, 255,  255, 182, 235,  255, 182, 219,  255, 182, 199,
255, 182, 182,  255, 199, 182,  255, 219, 182,  255, 235, 182,
255, 255, 182,  235, 255, 182,  219, 255, 182,  199, 255, 182,
182, 255, 182,  182, 255, 199,  182, 255, 219,  182, 255, 235,
182, 255, 255,  182, 235, 255,  182, 219, 255,  182, 199, 255,
  0,   0, 113,   28,   0, 113,   56,   0, 113,   85,   0, 113,
113,   0, 113,  113,   0,  85,  113,   0,  56,  113,   0,  28,
113,   0,   0,  113,  28,   0,  113,  56,   0,  113,  85,   0,
113, 113,   0,   85, 113,   0,   56, 113,   0,   28, 113,   0,
  0, 113,   0,    0, 113,  28,    0, 113,  56,    0, 113,  85,
  0, 113, 113,    0,  85, 113,    0,  56, 113,    0,  28, 113,
 56,  56, 113,   69,  56, 113,   85,  56, 113,   97,  56, 113,
113,  56, 113,  113,  56,  97,  113,  56,  85,  113,  56,  69,
113,  56,  56,  113,  69,  56,  113,  85,  56,  113,  97,  56,
113, 113,  56,   97, 113,  56,   85, 113,  56,   69, 113,  56,
 56, 113,  56,   56, 113,  69,   56, 113,  85,   56, 113,  97,
 56, 113, 113,   56,  97, 113,   56,  85, 113,   56,  69, 113,
 81,  81, 113,   89,  81, 113,   97,  81, 113,  105,  81, 113,
113,  81, 113,  113,  81, 105,  113,  81,  97,  113,  81,  89,
113,  81,  81,  113,  89,  81,  113,  97,  81,  113, 105,  81,
113, 113,  81,  105, 113,  81,   97, 113,  81,   89, 113,  81,
 81, 113,  81,   81, 113,  89,   81, 113,  97,   81, 113, 105,
 81, 113, 113,   81, 105, 113,   81,  97, 113,   81,  89, 113,
  0,   0,  65,   16,   0,  65,   32,   0,  65,   48,   0,  65,
 65,   0,  65,   65,   0,  48,   65,   0,  32,   65,   0,  16,
 65,   0,   0,   65,  16,   0,   65,  32,   0,   65,  48,   0,
 65,  65,   0,   48,  65,   0,   32,  65,   0,   16,  65,   0,
  0,  65,   0,    0,  65,  16,    0,  65,  32,    0,  65,  48,
  0,  65,  65,    0,  48,  65,    0,  32,  65,    0,  16,  65,
 32,  32,  65,   40,  32,  65,   48,  32,  65,   56,  32,  65,
 65,  32,  65,   65,  32,  56,   65,  32,  48,   65,  32,  40,
 65,  32,  32,   65,  40,  32,   65,  48,  32,   65,  56,  32,
 65,  65,  32,   56,  65,  32,   48,  65,  32,   40,  65,  32,
 32,  65,  32,   32,  65,  40,   32,  65,  48,   32,  65,  56,
 32,  65,  65,   32,  56,  65,   32,  48,  65,   32,  40,  65,
 44,  44,  65,   48,  44,  65,   52,  44,  65,   60,  44,  65,
 65,  44,  65,   65,  44,  60,   65,  44,  52,   65,  44,  48,
 65,  44,  44,   65,  48,  44,   65,  52,  44,   65,  60,  44,
 65,  65,  44,   60,  65,  44,   52,  65,  44,   48,  65,  44,
 44,  65,  44,   44,  65,  48,   44,  65,  52,   44,  65,  60,
 44,  65,  65,   44,  60,  65,   44,  52,  65,   44,  48,  65,
  0,   0,   0,    0,   0,   0,    0,   0,   0,    0,   0,   0,
  0,   0,   0,    0,   0,   0,    0,   0,   0,    0,   0,   0 ]

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Syntax: vgashow.py filename")
        sys.exit(1)
    file = sys.argv[1]
    main(file)  
