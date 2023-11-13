## Server-Client

Server-client communication simulation.
exploring the impact of varying job production rates (client's lambda) and job handling times (server's mu) during a 10-second sessions.


## Skills Demonstrated in the Project

- C programming
- Concurrency and Multi-threaded programming
- Process synchronization
- Dynamic data structures manipulation
- Data simulation using Python script
- Statistical analysis of simulation results
- Data visualization using Matplotlib


## Project Structure

The project consists of two source codes: "server.c" and "client.c". Each source code to be compiled into an executable file.

- client:
Simulates a client connecting to a server, generating jobs in a Poisson process with a specified parameter (lambda) for a given duration (T seconds).
While producing and sending jobs to the server, the client also receives job status notifications, updating a log file.
The connection is closed from the client side after T seconds.

- server:
Simulates a server receiving jobs from a client, managing them in a finite-size queue with a FIFO mechanism, and executing them with a specified service rate (mu).
While handling jobs, the server receives new jobs, notifies the client of job status (finished or dropped), and updates its log file.
After the client closes the connection, the server completes all pending jobs in a FIFO order before closing the connection.

- simulation script:
Runs the simulation multiple times with different parameters and visualizes the results by comparing various simulation runs.


## Build and Run Instructions

1. Compile Source Codes:
   - Compile "server.c" and "client.c" using a C compiler to executables "server.exe" and "client.exe" respectivly.

2. Run Simulation Script:
   - Ensure you have Python3 installed.
   - Open a terminal in the project directory and execute:
     "python3 simulation.py"

3. Execution Notes:
   - Make sure both executables are in the same directory as the script for proper execution.

4. Adjustment:
   - Adjustments my be needed, based on your development environment.
