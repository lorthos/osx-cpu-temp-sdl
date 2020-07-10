/*
 * Apple System Management Control (SMC) Tool
 * Copyright (C) 2006 devnull
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <iostream>
#include "SDL.h"
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <SDL_ttf.h>
#include "smc.h"

static io_connect_t conn;

UInt32 _strtoul(char *str, int size, int base) {
    UInt32 total = 0;
    int i;

    for (i = 0; i < size; i++) {
        if (base == 16)
            total += str[i] << (size - 1 - i) * 8;
        else
            total += (unsigned char) (str[i] << (size - 1 - i) * 8);
    }
    return total;
}

void _ultostr(char *str, UInt32 val) {
    str[0] = '\0';
    sprintf(str, "%c%c%c%c",
            (unsigned int) val >> 24,
            (unsigned int) val >> 16,
            (unsigned int) val >> 8,
            (unsigned int) val);
}

kern_return_t SMCOpen(void) {
    kern_return_t result;
    io_iterator_t iterator;
    io_object_t device;

    CFMutableDictionaryRef matchingDictionary = IOServiceMatching("AppleSMC");
    result = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDictionary, &iterator);
    if (result != kIOReturnSuccess) {
        printf("Error: IOServiceGetMatchingServices() = %08x\n", result);
        return 1;
    }

    device = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (device == 0) {
        printf("Error: no SMC found\n");
        return 1;
    }

    result = IOServiceOpen(device, mach_task_self(), 0, &conn);
    IOObjectRelease(device);
    if (result != kIOReturnSuccess) {
        printf("Error: IOServiceOpen() = %08x\n", result);
        return 1;
    }

    return kIOReturnSuccess;
}

kern_return_t SMCClose() {
    return IOServiceClose(conn);
}

kern_return_t SMCCall(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure) {
    size_t structureInputSize;
    size_t structureOutputSize;

    structureInputSize = sizeof(SMCKeyData_t);
    structureOutputSize = sizeof(SMCKeyData_t);

#if MAC_OS_X_VERSION_10_5
    return IOConnectCallStructMethod(conn, index,
            // inputStructure
                                     inputStructure, structureInputSize,
            // ouputStructure
                                     outputStructure, &structureOutputSize);
#else
    return IOConnectMethodStructureIStructureO(conn, index,
                                               structureInputSize, /* structureInputSize */
                                               &structureOutputSize, /* structureOutputSize */
                                               inputStructure, /* inputStructure */
                                               outputStructure); /* ouputStructure */
#endif
}

kern_return_t SMCReadKey(UInt32Char_t key, SMCVal_t *val) {
    kern_return_t result;
    SMCKeyData_t inputStructure;
    SMCKeyData_t outputStructure;

    memset(&inputStructure, 0, sizeof(SMCKeyData_t));
    memset(&outputStructure, 0, sizeof(SMCKeyData_t));
    memset(val, 0, sizeof(SMCVal_t));

    inputStructure.key = _strtoul(key, 4, 16);
    inputStructure.data8 = SMC_CMD_READ_KEYINFO;

    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;

    val->dataSize = outputStructure.keyInfo.dataSize;
    _ultostr(val->dataType, outputStructure.keyInfo.dataType);
    inputStructure.keyInfo.dataSize = val->dataSize;
    inputStructure.data8 = SMC_CMD_READ_BYTES;

    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;

    memcpy(val->bytes, outputStructure.bytes, sizeof(outputStructure.bytes));

    return kIOReturnSuccess;
}

double SMCGetTemperature(char *key) {
    SMCVal_t val;
    kern_return_t result;

    result = SMCReadKey(key, &val);
    if (result == kIOReturnSuccess) {
        // read succeeded - check returned value
        if (val.dataSize > 0) {
            if (strcmp(val.dataType, DATATYPE_SP78) == 0) {
                // convert sp78 value to temperature
                int intValue = val.bytes[0] * 256 + (unsigned char) val.bytes[1];
                return intValue / 256.0;
            }
        }
    }
    // read failed
    return 0.0;
}

// Requires SMCOpen()
std::string readCpuTemp() {
    double temperature = SMCGetTemperature(SMC_KEY_CPU_TEMP);
    std::ostringstream stringStream;
    stringStream << "CPU: " << temperature;
    return stringStream.str();
}

// Requires SMCOpen()
std::string readGpuTemp() {
    double temperature = SMCGetTemperature(SMC_KEY_GPU_TEMP);
    std::ostringstream stringStream;
    stringStream << "GPU: " << temperature;
    return stringStream.str();
}

// modifications for main.cpp

int main() {

    Uint32 last_tick = 0;
    Uint32 refresh_interval = 5000;
    Uint32 time_since_last_refresh = 0;

    SMCOpen();

    SDL_Renderer *renderer = NULL;
    SDL_Window *window;
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(
        "Simple Stats",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        320,
        30,
        SDL_WINDOW_OPENGL
    );

    SDL_RaiseWindow(window);

    if (NULL == window) {
        printf("Could not create window: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() < 0) {
        printf("Could not create window: %s\n", SDL_GetError());
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);


    TTF_Font *font = TTF_OpenFont("Carlito-Regular.ttf", 16);


    SDL_Color color = {255, 255, 255};
    bool quit = false;
    SDL_Event e;

    while (!quit) {
        //Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            //User requests quit
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }

        bool refresh = false;
        auto ticks = SDL_GetTicks();
        time_since_last_refresh += (ticks - last_tick);
        if (time_since_last_refresh > refresh_interval) {
            time_since_last_refresh = 0;
            refresh = true;
        }
        last_tick = ticks;

        if (refresh) {

            int texW = 0;
            int texH = 0;
            std::ostringstream stringStream;
            stringStream << "" << readCpuTemp() << " , " << readGpuTemp();
            SDL_Surface *surface = TTF_RenderText_Solid(font, stringStream.str().c_str(), color);
            SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);


            SDL_QueryTexture(texture, NULL, NULL, &texW, &texH);
            SDL_Rect dstrect = {0, 0, texW, texH};

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, &dstrect);
            SDL_RenderPresent(renderer);

            SDL_DestroyTexture(texture);
            SDL_FreeSurface(surface);

        } else {
            SDL_Delay(100);
        }


    }


    SMCClose();
    TTF_Quit();
    TTF_CloseFont(font);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
