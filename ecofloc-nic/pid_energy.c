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


#include"pid_energy.h"



nic_features features;

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
    time_t start_time, current_time;
    double total_energy = 0.0;
    char command[512];
    char output[1024];

    signal(SIGINT, handle_sigint);

    start_time = time(NULL);

   

    while (keep_running )
    {
        char buffer[128];
        FILE *file = stdin;
        if ( is_data_available(file)) {
            fgets(buffer, sizeof(buffer), stdin);
            printf("Received from Python: %s", buffer);
            break;
        }
        // Check if the process still exists
        if (kill(pid, 0) == -1)
        {
            printf("Process %d does not exist or was killed\n", pid);
            break;
        }

        // Construct and run the command to gather network usage
        sprintf(command, "timeout %d nethogs %s -t -P %d -v 0 -d 1 -a | awk '/%d/ {print $(NF-1), $NF}'", 
        interval_ms / 1000, features.iface, pid, pid);
        printf("AQUI\n");
        //printf("command->%s\n",command);
        FILE *fp = popen(command, "r");
        if (fp == NULL)
        {
            perror("Failed to run Nethogs");
            break;
        }
        printf("AQUI_2\n");

        while (fgets(output, sizeof(output) - 1, fp) != NULL)
        {
            printf("output->%s\n",output);
            double upload_rate, download_rate;
            if (sscanf(output, "%lf %lf", &upload_rate, &download_rate) != 2)
            {
                //Enable to debug;
                //fprintf(stderr, "Failed to parse  the nethogs output: %s", output);
                continue;
            }

            // printf("upload->%f\n",upload_rate);
            // printf("download->%f\n",download_rate);

            double upload_power = features.upload_power * (upload_rate / features.upload_max_rate);
            double download_power = features.download_power * (download_rate / features.download_max_rate);
            
            // printf("DL POWER->%f\n",features.download_power);
            // printf("UP POWER->%f\n",features.upload_power);
            // printf("upload RATE MAX->%ld\n",features.upload_max_rate);
            // printf("download RATE MAX->%ld\n",features.download_max_rate);
            
            double avg_interval_power = upload_power + download_power;

            // Calculate interval power and energy
            double interval_seconds = (double) interval_ms / 1000.0;
            double interval_energy = avg_interval_power * interval_seconds; // Energy in joules for the interval

            write_results(pid, time(NULL) - start_time, avg_interval_power,interval_energy);

            total_energy += interval_energy; // Total energy in joules
        }
        printf("AQUI_2_5\n");
        pclose(fp);
        printf("AQUI_2_6\n");
        current_time = time(NULL);
        printf("AQUI_3\n");
    }
    return total_energy; // Return total energy in joules
}
