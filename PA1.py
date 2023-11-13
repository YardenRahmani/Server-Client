import subprocess
import time
#import pandas ### remove commented lines to create excel files
import matplotlib.pyplot as plt

def run_server_client(i): # run client and server once, with param i
    server = subprocess.Popen(["server.exe", str(10326), "511", str(i), "5.0", "15"])
    time.sleep(0.1)
    client = subprocess.Popen(["client.exe", "127.0.0.1", str(10326), "111", str(i), str(4.55+0.05*i), "10.0"])
    server.wait()
    client.wait()

def make_graphs(ro): # read data from log files and draw graphs
    max_size = [0 for x in range(10)]
    avg_size = [0 for x in range(10)]
    drops = [0 for x in range(10)]
    min_total_time = [0 for x in range(10)]
    max_total_time = [0 for x in range(10)]
    avg_total_time = [0 for x in range(10)]
    Q_size = [0 for x in range(10)]
    Q_size_time = [0 for x in range(10)]
    for i in range(10): # read from each log file of 10 runs
        filename = "server_" + str(i) + ".log"
        server_file = open(filename, "r")
        filename = "client_" + str(i) + ".log"
        client_file = open(filename, "r")
        line = server_file.readline()
        lines = server_file.readlines()[1:]
        server_file.close()
        Q_size[i] = [0 for x in range(len(lines))]
        Q_size_time[i] = [0 for x in range(len(lines))]
        for j in range(len(lines)):
            [Q_size_time[i][j], Q_size[i][j]] = lines[j].split()
        tokens = line.split(", ")
        max_size[i] = float(tokens[3].split()[2])
        avg_size[i] = tokens[4].split()[2]
        line = client_file.readline()
        tokens = line.split(", ")
        drops[i] = tokens[4].split()[2]
        min_total_time[i] = tokens[5].split()[2]
        max_total_time[i] = tokens[6].split()[2]
        avg_total_time[i] = tokens[7].split()[2]
    # figure 1
    colors = ['b', 'g', 'r', 'c', 'm', 'y', 'k', 'purple', 'orange', 'pink']
    fig, ax1 = plt.subplots()
    for i in range(9):
        ax1.plot(Q_size_time[9], Q_size[i] + [0 for x in range(len(Q_size[9])-len(Q_size[i]))], colors[i], label='run'+str(i))
    ax1.plot(Q_size_time[9], Q_size[9], colors[9], label='run9')
    ax1.set_xlabel('time[msec]')
    ax1.set_ylabel('Number of jobs')
    ax1.set_title('queue size as a function of time and Ro')
    ax1.legend()
    # figure 2
    fig, ax2 = plt.subplots()
    ax2.plot(ro, max_size, 'c-', label='max queue size')
    ax2.plot(ro, avg_size, 'm-', label='avg queue size')
    ax2.set_xlabel('Ro')
    ax2.set_ylabel('Number of jobs')
    ax2.set_title('Min, Max and Average job time as a function of Ro')
    ax2.legend()
    # figure 3
    fig, ax3 = plt.subplots()
    ax3.plot(ro, drops, 'c-', label='total drops')
    ax3.set_xlabel('Ro')
    ax3.set_ylabel('Number of jobs')
    ax3.set_title('total job drops as a function of Ro')
    ax3.legend()
    # figure 4
    fig, ax4 = plt.subplots()
    ax4.plot(ro, min_total_time, 'b-', label='min job time')
    ax4.plot(ro, avg_total_time, 'r-', label='avg job time')
    ax4.plot(ro, max_total_time, 'g-', label='max job time')
    ax4.set_xlabel('Ro')
    ax4.set_ylabel('time[msec]')
    ax4.set_title('Min, Max and Average job time as a function of Ro')
    ax4.legend()

    plt.show()

for i in range(10): # run client and server 10 times with parameter i
    print(f"Running iteration {i}")
    run_server_client(i)
ro = [5/(4.55+0.05*i) for i in range(10)]
make_graphs(ro)