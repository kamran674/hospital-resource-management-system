# hospital-resource-management-system

A Linux-based Operating Systems semester project that simulates a real-world hospital emergency room using core OS concepts such as process management, inter-process communication (IPC), CPU scheduling, synchronization, threading, semaphores, shared memory, and memory allocation strategies.

---

# Project Overview

The system simulates how patients arrive in a hospital emergency room, receive a triage priority, wait in scheduling queues, and get allocated to hospital beds based on care requirements.

The project demonstrates multiple operating system concepts working together inside a single integrated application.

## Main Features

* Patient triage system with severity-based priority assignment
* Multi-process hospital simulation using `fork()` and `exec()`
* Anonymous pipes and named FIFOs for IPC
* Shared memory bed allocation bitmap
* Priority-based patient scheduling
* POSIX threads for receptionist, scheduler, and nurse management
* Mutexes and condition variables for synchronization
* Semaphores for ICU and Isolation capacity control
* Best-Fit, First-Fit, and Worst-Fit memory allocation strategies
* Fragmentation analysis and paging simulation
* Automated stress testing with concurrent patient arrivals
* Runtime logs for scheduling and memory management

---

# OS Concepts Demonstrated

| OS Concept               | Implementation                                       |
| ------------------------ | ---------------------------------------------------- |
| Process Management       | `fork()`, `execv()`, `waitpid()`, `SIGCHLD` handling |
| IPC                      | Pipes, Named FIFO, Shared Memory                     |
| CPU Scheduling           | FCFS, Priority Scheduling                            |
| Synchronization          | Mutexes, Condition Variables                         |
| Threads                  | POSIX Threads (pthread)                              |
| Semaphores               | ICU/Isolation admission control                      |
| Memory Management        | Best-Fit, First-Fit, Worst-Fit                       |
| Fragmentation            | External & Internal Fragmentation Tracking           |
| Paging                   | Simulated Page Table                                 |
| Linux System Programming | Shell scripting, Makefile, process control           |

---

# System Architecture

The project consists of the following components:

## Admissions Manager

The central process responsible for:

* Managing patient admissions
* Running the scheduler
* Allocating beds
* Maintaining shared memory
* Creating patient processes

## Patient Simulator

A child process created for each admitted patient.
It simulates:

* Treatment duration
* Patient lifecycle events
* Discharge notifications

## Triage Script

A shell script that:

* Accepts patient details
* Validates input
* Computes patient priority
* Sends records to the admissions manager

## Nurse Thread Pool

Multiple nurse threads monitor occupied beds and free resources once treatment completes.

## Shared Memory Ward

Represents the hospital ward as a contiguous memory model with:

* ICU Beds
* Isolation Beds
* General Ward Beds

---

# Bed Allocation Strategies

The system supports three memory allocation strategies:

## Best-Fit

Allocates the smallest available partition capable of satisfying the request.

## First-Fit

Allocates the first suitable free partition.

## Worst-Fit

Allocates the largest available free partition.

Allocation strategy can be selected at runtime.

---

# Synchronization Features

* Mutex-protected shared bed bitmap
* Producer-consumer queue synchronization
* Condition variables for bed availability signaling
* Counting semaphores for ICU and Isolation limits
* Safe concurrent admission handling

---

# Scheduling Algorithms

Implemented scheduling simulations include:

* First Come First Served (FCFS)
* Priority Scheduling

The system also calculates:

* Average Waiting Time
* Average Turnaround Time

Logs are stored in:

```bash
logs/schedule_log.txt
```

---

# Memory Management Features

The ward is modeled as contiguous memory where each unit represents hospital care capacity.

Implemented memory management concepts:

* Dynamic partition allocation
* Free-list management
* Coalescing adjacent free partitions
* External fragmentation calculation
* Paging simulation
* Internal fragmentation reporting

Logs are stored in:

```bash
logs/memory_log.txt
```

---

# Project Structure

```bash
hospital/
│
├── src/
│   ├── admissions.c
│   ├── patient_simulator.c
│   ├── scheduler.c
│   ├── bed_allocator.c
│   └── hospital.h
│
├── scripts/
│   ├── start_hospital.sh
│   ├── stop_hospital.sh
│   ├── triage.sh
│   ├── stress_test.sh
│   └── send_patient.sh
│
├── logs/
│   ├── memory_log.txt
│   ├── schedule_log.txt
│   └── patient_records.dat
│
├── makefile
└── README.md
```

---

# Build Instructions

## Compile the Project

```bash
make all
```

## Start Hospital System

```bash
make run
```

or

```bash
./scripts/start_hospital.sh
```

## Add a Patient

```bash
./scripts/triage.sh Hamza 21 9
```

## Run Stress Test

```bash
./scripts/stress_test.sh
```

## Stop Hospital System

```bash
./scripts/stop_hospital.sh
```

## Clean Build Files

```bash
make clean
```

---

# Example Simulation Output

```text
[TRIAGE] Patient: Ahmed | Severity: 9 | Priority: 1
[ADMISSION] ICU bed allocated to Patient ID 101
[PATIENT] Treatment started...
[NURSE] Patient discharged. Bed released.
[SCHEDULER] Waiting patient admitted successfully.
```

---

# Technical Highlights

* Handles multiple concurrent patient processes
* Demonstrates real-time synchronization
* Uses Linux system-level APIs extensively
* Simulates realistic hospital resource management
* Implements OS scheduling and memory algorithms in a practical scenario

---

# Tools & Technologies

* C Programming Language
* POSIX Threads (`pthread`)
* Linux System Calls
* Shared Memory APIs
* Named Pipes (FIFO)
* Semaphores
* Bash Scripting
* GCC Compiler
* Makefile

---

# Learning Outcomes

This project helped in understanding:

* Multi-process application design
* Concurrent programming challenges
* Synchronization and race-condition prevention
* IPC mechanisms in Linux
* Real-world scheduling algorithms
* Memory allocation and fragmentation handling
* Linux system programming and debugging

---

# Future Improvements

Possible future enhancements:

* Graphical monitoring dashboard
* Multi-ward hospital support
* Network-based remote triage
* Database integration
* Real-time analytics
* Advanced scheduling algorithms

---

# License

This project is developed for educational purposes as part of the Operating Systems Lab course.

---


