# Tech Support Scheduling Simulator

A command-line simulation written in C where **4 technicians** handle a list of
tech-support tasks. The program advances a clock in discrete time units and
assigns tasks to free technicians using a selectable scheduling algorithm.

> Programming Lab I, Project 1

## Features

- Reads tasks from a text file, validates every line and reports rejected ones
- Four scheduling algorithms:
  - **Shortest Job (SJ)**: shortest duration first
  - **Priority**: lowest priority number first (1 = most important)
  - **Round Robin**: shared FIFO queue with a configurable time quantum
  - **Combined SJ+P (custom)**: picks the technician-task pair with the lowest score,
    `score = (duration + travel cost) * priority`
- **Travel cost**: if a technician moves to a task in a different location,
  it costs 1 time unit of travel (can be disabled)
- Interactive menu: advance time by 1 or N units, jump to a given time
  (going backwards replays the simulation from the start), list or query tasks,
  show technician status
- Statistics: average waiting time, average time in system, makespan,
  per-technician and overall utilization, total travel time
- Comparison mode: runs all four algorithms on the same input and prints one table

## Build

```bash
gcc -Wall -Wextra -o simulator main.c
```

## Run

Interactive mode (the program asks for the task file path):

```bash
./simulator
```

Without travel cost:

```bash
./simulator --gecissiz
```

Compare all algorithms (optional quantum for Round Robin, default 2):

```bash
./simulator --karsilastir tasks.txt 3
```

## Input file format

One task per line, 6 whitespace-separated fields:

```
ID  ARRIVAL_TIME  DURATION  PRIORITY  TASK_TYPE  LOCATION
```

| Field | Rule |
|---|---|
| ID | Positive integer, must be unique |
| ARRIVAL_TIME | Integer >= 0 |
| DURATION | Integer >= 1 |
| PRIORITY | 1 to 5 (1 = highest) |
| TASK_TYPE | One word, max 39 characters |
| LOCATION | `LAB_A`, `LAB_B`, `DERSLIK_1`, `DERSLIK_2` or `DERSLIK_3` |

Empty lines and lines starting with `#` are ignored. Invalid lines are rejected
with a message and the rest of the file is still processed. Maximum 20,000 tasks.

Example:

```
# id arrival duration priority type location
1 0 3 2 NETWORK LAB_A
2 0 4 1 SOFTWARE DERSLIK_1
3 2 2 3 HARDWARE LAB_B
4 5 1 1 NETWORK LAB_A
```

## Menu

| Option | Action |
|---|---|
| 1 | Select or change the algorithm (restarts the simulation) |
| 2 | List all tasks |
| 3 | List tasks by status |
| 4 | Query a task by ID |
| 5 | Show technician status |
| 6 | Advance time by 1 unit |
| 7 | Advance time by N units |
| 8 | Go to a specific time |
| 9 | Show statistics |
| 10 | Reset the simulation |
| 0 | Exit |

## How it works

Each call to the time-step function advances the clock by exactly one unit, in this order:

1. Working technicians progress their task (or spend the unit travelling)
2. Finished tasks are marked as done and their technicians are freed
3. Round Robin only: tasks whose time slice ended go back to waiting
4. Newly arrived tasks enter the system
5. Free technicians are assigned new tasks by the selected algorithm

When the system is completely idle, the clock jumps straight to the next arrival.

## Metrics

- Waiting time = completion - arrival - duration
- Time in system = completion - arrival
- Utilization = 100 * worked units / elapsed time (travel is not counted as work)
- Makespan = completion time of the last task

## Authors


- Alina Maksatbek kyzy