//
// Created by youssef on 5/21/2024.
//

#ifndef WEBCLIENT_SYS_INFO_H
#define WEBCLIENT_SYS_INFO_H
#include <string>

namespace sys_info {
    struct sys_mem_info {
        double TotalPhysicalMemory;
        double TotalUsedMemory;
    };

    double GetCPUUsage(long wait_for_reading_ms = 1000);
    sys_mem_info GetMemoryUsage();
    std::string GetAsJSON(long wait_for_reading_ms = 1000);
};


#endif //WEBCLIENT_SYS_INFO_H
