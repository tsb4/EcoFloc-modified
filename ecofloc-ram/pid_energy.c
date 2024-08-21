 
/*
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
*/



#include "pid_energy.h"
#include "results_map.h"




void strip_non_digit(char *src, char *dst) 
{
    while (*src) 
    {
        if (isdigit(*src) || *src == '.')
            *dst++ = *src;
        src++;
    }
    *dst = '\0';
}

volatile sig_atomic_t keep_running = 1;

void handle_sigint(int sig) 
{
    keep_running = 0;
}
int is_data_available(FILE *file) {
    int fd = fileno(file);  // Get the file descriptor from the FILE * stream
    if (fd == -1) {
        perror("fileno");
        return 0;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    // Set timeout to 0 to make it non-blocking
    struct timeval timeout = {0, 0};

    int result = select(fd + 1, &read_fds, NULL, NULL, &timeout);

    if (result == -1) {
        perror("select");
        return 0;
    }

    return FD_ISSET(fd, &read_fds);
}

double pid_energy(int pid, int interval_ms, int timeout_s)
{
    time_t start_time = time(NULL);
    char command[512];
    char output[1024];
    char clean_output[1024];

    double interval_s = interval_ms / 1000.0;
    double total_energy = 0.0;

    double final_mem_loads = 0.0;
    double final_mem_dram_loads = 0.0;
    double final_mem_stores = 0.0;


    while (keep_running)
    {
        char buffer[128];
        FILE *file = stdin;
        if ( is_data_available(file)) {
            fgets(buffer, sizeof(buffer), stdin);
            printf("Received from Python: %s", buffer);
            break;
        }
        
        // Construct the command to run perf
        //printf("pid: %d\n", pid);
        sprintf(command, "perf stat -e mem-stores,mem-loads,L1-dcache-loads,L1-dcache-stores,mem_load_l3_miss_retired.local_dram,mem_load_l3_miss_retired.remote_dram -p %d --timeout=%d 2>&1", pid, interval_ms);

        // Trigger perf
        FILE *fp = popen(command, "r");
        if (fp == NULL)
        {
            perror("Failed to run command");
            exit(1);
        }

        // Variables to hold parsed values
        double mem_stores = 0, mem_loads = 0;
        double cpu_core_mem_stores = 0.0;
        double cpu_core_mem_loads = 0.0;
        double avg_interval_power = 0.0;
        double interval_energy = 0.0;

        // Two possible output formats in perf: with and without the "cpu_core" prefix 
        int case_type = 0;

        signal(SIGINT, handle_sigint);
        //printf("%d %d\n", pid, interval_ms);
        // If there is a new line in the file
        while (fgets(output, sizeof(output) - 1, fp) != NULL)
        {
            //printf("Output1: %s\n", output);
            // Truncates the string if "(" is present
            char *percent_ptr = strchr(output, '(');
            if (percent_ptr != NULL)
                *percent_ptr = '\0';

            // Remove trailing whitespace
            for (int j = strlen(output) - 1; j >= 0; j--)
            {
                if (isspace(output[j]))
                    output[j] = '\0';
                else
                    break;
            }

            if (strstr(output, "cpu_core/mem-stores/") != NULL || strstr(output, "cpu_core/mem-loads/") != NULL)
            {
                case_type = 2;
                if (strstr(output, "cpu_core/mem-stores/") != NULL)
                {
                    strip_non_digit(output, clean_output);
                    sscanf(clean_output, "%lf", &cpu_core_mem_stores);
                }
                else if (strstr(output, "cpu_core/mem-loads/") != NULL)
                {
                    strip_non_digit(output, clean_output);
                    sscanf(clean_output, "%lf", &cpu_core_mem_loads);
                }
            }
            else if (strstr(output, "mem-stores") != NULL || strstr(output, "mem-loads") != NULL || strstr(output, "L1-dcache-loads") != NULL
                     || strstr(output, "mem_load_l3_miss_retired.local_dram") != NULL || strstr(output, "mem_load_l3_miss_retired.remote_dram") != NULL)
            {
                if (case_type != 2)
                {
                    case_type = 1;
                    if (strstr(output, "mem-stores") != NULL)
                    {
                        strip_non_digit(output, clean_output);
                        sscanf(clean_output, "%lf", &mem_stores);
                        final_mem_stores += mem_stores;
                    }
                    //else if (strstr(output, "mem-loads") != NULL)
                    //{
                    //    strip_non_digit(output, clean_output);
                    //    sscanf(clean_output, "%lf", &mem_loads);
                    //}
                    else if (strstr(output, "L1-dcache-loads") != NULL)
                    {
                        strip_non_digit(output, clean_output);
                        sscanf(clean_output, "%lf", &mem_loads);
                        final_mem_loads += mem_loads;
                        mem_loads = 0.0;
                    }
                    else if (strstr(output, "mem_load_l3_miss_retired.local_dram") != NULL)
                    {
                        strip_non_digit(output, clean_output);
                        sscanf(clean_output, "%lf", &mem_loads);
                        final_mem_dram_loads += mem_loads;
                    }else if (strstr(output, "mem_load_l3_miss_retired.remote_dram") != NULL)
                    {
                        strip_non_digit(output, clean_output);
                        sscanf(clean_output, "%lf", &mem_loads);
                        final_mem_dram_loads += mem_loads;
                    }
                }
            }
        }

        pclose(fp);
        //printf("case %d\n", case_type);
        // Calculate RAM active energy consumption
        double total_stores = 0.0;
        double total_loads = 0.0;
        double ram_act = 0.0;
        if (case_type == 1){
            ram_act = (mem_loads * 6.6) + (mem_stores * 8.7);
            total_stores += mem_stores;
            total_loads += mem_loads;
            
        }
        else if (case_type == 2){
            ram_act = (cpu_core_mem_stores * 8.7) + (cpu_core_mem_loads * 6.6);
            total_stores += cpu_core_mem_stores;
            total_loads += cpu_core_mem_loads;
        
        }if(ram_act>0.0){
            //printf("TAM_ACT: %f %f %f\n", ram_act, total_loads, total_stores);
        }
        
        // Total RAM power
        if (ram_act != 0)
        {
            interval_energy = ram_act / 1000000000; // Convert from nanoJoules to Joules
            avg_interval_power = interval_energy / interval_s; // Average interval power
        }

        // Write results
        write_results(pid, time(NULL) - start_time, avg_interval_power, interval_energy);

        total_energy += interval_energy;

        // struct timespec interval = {0, interval_ms * 1000000};
        // nanosleep(&interval, NULL);
    }
    // Open CSV file for appending
    FILE* file = fopen("/home/tiago/Documents/huggingFaceAnalaysis/energyConsumption/mem_ops.csv", "a");
    if (file == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    // Write data to CSV file
    fprintf(file, "%d,%f,%f,%f\n", pid, final_mem_loads, final_mem_stores, final_mem_dram_loads);

    // Close file
    fclose(file);

    //printf("Data added to %s\n", csv_file);

    return total_energy; // Total energy in Joules
}
