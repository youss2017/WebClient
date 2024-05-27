//
// Created by youssef on 5/21/2024.
//

#include "sys_info.h"

#include <windows.h>
#include <pdh.h>
#include <iostream>
#include <sstream>

#pragma comment(lib, "pdh.lib")

using namespace std;

double sys_info::GetCPUUsage(long wait_for_reading_ms) {
    PDH_HQUERY cpuQuery;
    PDH_HCOUNTER cpuTotal;
    PDH_FMT_COUNTERVALUE counterVal;

    // Create a query
    if (PdhOpenQuery(NULL, NULL, &cpuQuery) != ERROR_SUCCESS) {
        std::cerr << "PdhOpenQuery failed" << std::endl;
        return -1.0;
    }

    // Add a counter to the query
    if (PdhAddCounter(cpuQuery, "\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal) != ERROR_SUCCESS) {
        std::cerr << "PdhAddCounter failed" << std::endl;
        return -1.0;
    }

    // Collect the data
    if (PdhCollectQueryData(cpuQuery) != ERROR_SUCCESS) {
        std::cerr << "PdhCollectQueryData failed" << std::endl;
        return -1.0;
    }

    // Sleep for a second to get an accurate reading
    Sleep(wait_for_reading_ms);

    // Collect the data again
    if (PdhCollectQueryData(cpuQuery) != ERROR_SUCCESS) {
        std::cerr << "PdhCollectQueryData failed" << std::endl;
        return -1.0;
    }

    // Get the formatted counter value
    if (PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, nullptr, &counterVal) != ERROR_SUCCESS) {
        std::cerr << "PdhGetFormattedCounterValue failed" << std::endl;
        return -1.0;
    }

    // Close the query
    PdhCloseQuery(cpuQuery);
    if(counterVal.doubleValue < 0)
        return 0;
    return counterVal.doubleValue;
}

sys_info::sys_mem_info sys_info::GetMemoryUsage() {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);

        if (GlobalMemoryStatusEx(&memInfo)) {
            double totalPhysMem = memInfo.ullTotalPhys;
            double physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

            if(totalPhysMem < 0)
                totalPhysMem = 0;
            if(physMemUsed < 0)
                physMemUsed = 0;

            //std::cout << "Total Physical Memory: " << totalPhysMem / (1024 * 1024) << " MB" << std::endl;
            //std::cout << "Used Physical Memory: " << physMemUsed / (1024 * 1024) << " MB" << std::endl;
            //std::cout << "Memory Usage: " << (physMemUsed * 100.0) / totalPhysMem << " %" << std::endl;

            return {
                totalPhysMem,
                physMemUsed
            };

        } else {
            std::cerr << "GlobalMemoryStatusEx failed" << std::endl;
        }
        return {};
}

std::string sys_info::GetAsJSON(long wait_for_reading_ms) {
    stringstream str;

    double cpu = GetCPUUsage(wait_for_reading_ms);
    sys_mem_info memInfo = GetMemoryUsage();

    str << "{\n";
    str << "\t\"cpu_usage\": " << cpu << ",\n";
    str << "\t\"total_physical_memory\": " << memInfo.TotalPhysicalMemory << ",\n";
    str << "\t\"total_used_memory\": " << memInfo.TotalUsedMemory << "\n";
    str << '}';

    return str.str();
}
