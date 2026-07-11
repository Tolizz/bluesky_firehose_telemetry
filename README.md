#  Bluesky Jetstream Firehose Monitor

This repository contains the source code and data analysis tools for the final project of the **Real-Time Embedded Systems** course.

The system is designed to run on a **Raspberry Pi Zero 2 W** (Linux) and performs asynchronous ingestion, JSON parsing, and strictly synchronous telemetry data logging from the global Bluesky network (AT Protocol) via the Jetstream WebSocket API.

##  Prerequisites

To compile and run the code on a Raspberry Pi, you will need the following packages:

```bash
# Update system packages
sudo apt update && sudo apt upgrade -y

# Install compiler and build tools (make, gcc)
sudo apt install build-essential -y

# Install the libwebsockets library for C
sudo apt install libwebsockets-dev -y

# Install Python3 and pip (for the exploratory script)
sudo apt install python3 python3-pip -y

```

*(The `cJSON` library is typically included directly in the project files. If not, it requires installing the `libcjson-dev` package).*


##  Part 1: Network Profiling with Python (`firehose.py`)

Before developing the final C application, a Python script (`firehose.py`) was created for initial network profiling, understanding the JSON message structure, and calculating the average data rate.

**Install Python dependencies:**

```bash
pip3 install websockets asyncio

```

**Run the profiling tool:**

```bash
python3 firehose.py

```

The script connects to the `wss://jetstream1.us-east.bsky.network` endpoint, counts incoming packets, and prints basic rate statistics to the console. Press `Ctrl+C` to stop it.


##  Part 2: Main System in C (Real-Time Embedded Application)

The final application was written in C, utilizing POSIX Threads (pthreads) to implement a Producer-Consumer architecture. It guarantees zero data loss, network resilience against connection drops (via Exponential Backoff), and highly accurate logging (Jitter < 1ms).

### 1. Compilation
This requires `main.c` and `Makefile`.

Navigate to the project directory and run the following command. The `Makefile` will handle the build process:

```bash
make

```

### 2. Execution

To start the real-time monitor and logger, run:

```bash
./bsky_monitor

```

### 3. Termination and Output Files

To terminate the program safely (preventing memory leaks), press **`Ctrl+C`**. The program will catch the signal, safely join the threads, and close the network connection.

During execution, the program generates two files:

* `metrics_log.txt`: A CSV-formatted log containing telemetry statistics (Timestamps, Commits, CPU usage, Buffer occupancy) recorded strictly once per second (1 Hz).
* `debug_log.txt`: An event log that records initialization, termination, and any network disconnections/reconnections (Network Drops & Backoff events).


##  Part 3: Data Analysis (MATLAB)

To generate the report's plots and calculate the final statistics (Jitter, Bursts, Server Downtime), the `process.m` script is provided.

**Instructions for generating plots:**

1. Transfer the `metrics_log.txt` file from the Raspberry Pi to your local machine.
2. Place it in the same directory as the `process.m` file.
3. Open MATLAB, set that directory as your *Current Folder*, and run the script.
4. The script will automatically filter the data for the exact 24-hour period, generate 3 charts (Jitter, Load vs Buffer, CPU Load), and print detailed statistics in the Command Window.


**Note:** This project was developed as a university assignment and tested on physical hardware (Raspberry Pi Zero 2 W), successfully meeting the strict timing requirements of User Space Linux real-time systems.
