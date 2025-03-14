#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <ctype.h>
#include <pthread.h>

// Global flag for signal handling
volatile sig_atomic_t keep_running = 1;

// Structure to represent a process
typedef struct Process {
    pid_t pid;
    pid_t ppid;
    char state;
} Process;

// Process tree information
pid_t root_pid;
pid_t child1_pid, child2_pid;
pid_t grandchild1_pid, grandchild2_pid, grandchild3_pid, grandchild4_pid;
pid_t greatgrandchild1_pid, greatgrandchild2_pid;
pid_t zombie1_pid, zombie2_pid, zombie3_pid;

// Signal handler for graceful termination
void handle_sigterm(int sig) {
    keep_running = 0;
}

// Function to print PID and PPID
void print_process_info(const char *name) {
    printf("%s - PID: %d, PPID: %d\n", name, getpid(), getppid());
    fflush(stdout);
}

// Function to create a zombie process
pid_t create_zombie() {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Zombie fork failed");
        return -1;
    }

    if (pid == 0) { // Child
        // Child immediately exits to become a zombie
        print_process_info("Zombie");
        exit(0);
    }

    // Don't wait for the child, making it a zombie
    return pid;
}

// ------------------------ PRCT FUNCTIONS ------------------------ //

// Function to check if a process exists
int process_exists(pid_t pid) {
    char path[256];
    sprintf(path, "/proc/%d", pid);
    return access(path, F_OK) == 0;
}

// Function to get process information (PPID and state)
int get_process_info(pid_t pid, Process *proc) {
    char path[256];
    FILE *fp;
    char buffer[1024];
    char comm[256];

    proc->pid = pid;

    sprintf(path, "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }

    if (fgets(buffer, sizeof(buffer), fp)) {
        // Parse the stat file for process info
        // Format is: pid (comm) state ppid ...
        sscanf(buffer, "%d %s %c %d", &proc->pid, comm, &proc->state, &proc->ppid);
    } else {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

// Function to check if a process is defunct (zombie)
int is_defunct(pid_t pid) {
    Process proc;
    if (!get_process_info(pid, &proc)) {
        return 0;
    }
    return proc.state == 'Z';
}

// Function to check if a given process is an ancestor of another process
int is_ancestor(pid_t ancestor, pid_t descendant) {
    if (ancestor == descendant) {
        return 1;
    }

    Process proc;
    pid_t current = descendant;

    while (current > 1) { // PID 1 is init, so we stop there
        if (!get_process_info(current, &proc)) {
            return 0;
        }

        if (proc.ppid == ancestor) {
            return 1;
        }

        current = proc.ppid;

        // Check if we're in a loop (shouldn't happen, but just in case)
        if (current == proc.pid) {
            break;
        }
    }

    return 0;
}

// Function to get all processes
void get_all_processes(Process **processes, int *count) {
    DIR *dir;
    struct dirent *entry;
    pid_t pid;

    *count = 0;
    *processes = NULL;

    dir = opendir("/proc");
    if (!dir) {
        perror("Failed to open /proc");
        return;
    }

    while ((entry = readdir(dir))) {
        // Check if the entry is a directory and named with digits (process ID)
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            pid = atoi(entry->d_name);

            *processes = realloc(*processes, (*count + 1) * sizeof(Process));
            if (get_process_info(pid, &(*processes)[*count])) {
                (*count)++;
            }
        }
    }

    closedir(dir);
}

// Function to get the descendants of a process
void get_descendants(pid_t root, pid_t **descendants, int *count) {
    Process *processes;
    int proc_count;

    *count = 0;
    *descendants = NULL;

    get_all_processes(&processes, &proc_count);

    // Build the list of descendants
    for (int i = 0; i < proc_count; i++) {
        if (processes[i].pid != root && is_ancestor(root, processes[i].pid)) {
            *descendants = realloc(*descendants, (*count + 1) * sizeof(pid_t));
            (*descendants)[*count] = processes[i].pid;
            (*count)++;
        }
    }

    free(processes);
}

// Function to get the immediate descendants (children) of a process
void get_immediate_descendants(pid_t parent, pid_t **children, int *count) {
    Process *processes;
    int proc_count;

    *count = 0;
    *children = NULL;

    get_all_processes(&processes, &proc_count);

    // Find processes with the specified parent
    for (int i = 0; i < proc_count; i++) {
        if (processes[i].ppid == parent) {
            *children = realloc(*children, (*count + 1) * sizeof(pid_t));
            (*children)[*count] = processes[i].pid;
            (*count)++;
        }
    }

    free(processes);
}

// Function to get non-direct descendants (not immediate children)
void get_non_direct_descendants(pid_t root, pid_t **descendants, int *count) {
    pid_t *all_descendants;
    int all_count;
    pid_t *immediate;
    int imm_count;

    *count = 0;
    *descendants = NULL;

    // Get all descendants
    get_descendants(root, &all_descendants, &all_count);

    // Get immediate descendants
    get_immediate_descendants(root, &immediate, &imm_count);

    // Find non-direct descendants (all descendants minus immediate descendants)
    for (int i = 0; i < all_count; i++) {
        int is_immediate = 0;
        for (int j = 0; j < imm_count; j++) {
            if (all_descendants[i] == immediate[j]) {
                is_immediate = 1;
                break;
            }
        }

        if (!is_immediate) {
            *descendants = realloc(*descendants, (*count + 1) * sizeof(pid_t));
            (*descendants)[*count] = all_descendants[i];
            (*count)++;
        }
    }

    free(all_descendants);
    free(immediate);
}

// Function to get defunct descendants
void get_defunct_descendants(pid_t root, pid_t **defunct, int *count) {
    pid_t *descendants;
    int desc_count;

    *count = 0;
    *defunct = NULL;

    get_descendants(root, &descendants, &desc_count);

    for (int i = 0; i < desc_count; i++) {
        if (is_defunct(descendants[i])) {
            *defunct = realloc(*defunct, (*count + 1) * sizeof(pid_t));
            (*defunct)[*count] = descendants[i];
            (*count)++;
        }
    }

    free(descendants);
}

// Function to get the grandchildren of a process
void get_grandchildren(pid_t root, pid_t **grandchildren, int *count) {
    pid_t *children;
    int child_count;

    *count = 0;
    *grandchildren = NULL;

    get_immediate_descendants(root, &children, &child_count);

    for (int i = 0; i < child_count; i++) {
        pid_t *child_children;
        int cc_count;

        get_immediate_descendants(children[i], &child_children, &cc_count);

        for (int j = 0; j < cc_count; j++) {
            *grandchildren = realloc(*grandchildren, (*count + 1) * sizeof(pid_t));
            (*grandchildren)[*count] = child_children[j];
            (*count)++;
        }

        free(child_children);
    }

    free(children);
}

// Function to get siblings of a process
void get_siblings(pid_t process_id, pid_t **siblings, int *count) {
    Process proc;
    pid_t parent;

    *count = 0;
    *siblings = NULL;

    if (!get_process_info(process_id, &proc)) {
        return;
    }

    parent = proc.ppid;

    Process *processes;
    int proc_count;

    get_all_processes(&processes, &proc_count);

    // Find processes with the same parent
    for (int i = 0; i < proc_count; i++) {
        if (processes[i].ppid == parent && processes[i].pid != process_id) {
            *siblings = realloc(*siblings, (*count + 1) * sizeof(pid_t));
            (*siblings)[*count] = processes[i].pid;
            (*count)++;
        }
    }

    free(processes);
}

// Function to get defunct siblings
void get_defunct_siblings(pid_t process_id, pid_t **defunct_siblings, int *count) {
    pid_t *siblings;
    int sib_count;

    *count = 0;
    *defunct_siblings = NULL;

    get_siblings(process_id, &siblings, &sib_count);

    for (int i = 0; i < sib_count; i++) {
        if (is_defunct(siblings[i])) {
            *defunct_siblings = realloc(*defunct_siblings, (*count + 1) * sizeof(pid_t));
            (*defunct_siblings)[*count] = siblings[i];
            (*count)++;
        }
    }

    free(siblings);
}

// Function to kill parents of zombie processes
void kill_parents_of_zombies(pid_t root) {
    pid_t *defunct;
    int defunct_count;

    get_defunct_descendants(root, &defunct, &defunct_count);

    for (int i = 0; i < defunct_count; i++) {
        Process zombie;
        if (get_process_info(defunct[i], &zombie)) {
            kill(zombie.ppid, SIGKILL);
        }
    }

    free(defunct);
}

// Function to handle prct command
void handle_prct_command(int argc, char *argv[]) {
    if (argc != 3) { // Ensure we have exactly 3 arguments (excluding the program name itself)
        fprintf(stderr, "Usage: prct root_pid process_id option\n");
        return;
    }

    char *root_pid_str = argv[0];
    char *process_id_str = argv[1];
    char *option = argv[2];

    // Check if root_pid_str and process_id_str are valid integers
    char *endptr_root, *endptr_process;
    pid_t root_pid = strtol(root_pid_str, &endptr_root, 10);
    pid_t process_id = strtol(process_id_str, &endptr_process, 10);

    if (*endptr_root != '\0' || *endptr_process != '\0') {
        fprintf(stderr, "Error: root_pid and process_id must be valid integers.\n");
        return;
    }

    // Check if the root process exists
    if (!process_exists(root_pid)) {
        fprintf(stderr, "Error: root process with PID %d does not exist\n", root_pid);
        return;
    }

    // Check if the process exists
    if (!process_exists(process_id)) {
        fprintf(stderr, "Error: process with PID %d does not exist\n", process_id);
        return;
    }

    // Rest of the handle_prct_command function (same as before)
    // Handle options
    if (strcmp(option, "-dc") == 0) {
        // Count defunct descendants
        pid_t *defunct;
        int count;
        get_defunct_descendants(process_id, &defunct, &count);
        printf("%d\n", count);
        free(defunct);
    } else if (strcmp(option, "-ds") == 0) {
        // List non-direct descendants
        pid_t *non_direct;
        int count;
        get_non_direct_descendants(process_id, &non_direct, &count);

        if (count == 0) {
            printf("No non-direct descendants\n");
        } else {
            for (int i = 0; i < count; i++) {
                printf("%d\n", non_direct[i]);
            }
        }
        free(non_direct);
    } else if (strcmp(option, "-id") == 0) {
        // List immediate descendants
        pid_t *immediate;
        int count;
        get_immediate_descendants(process_id, &immediate, &count);

        if (count == 0) {
            printf("No direct descendants\n");
        } else {
            for (int i = 0; i < count; i++) {
                printf("%d\n", immediate[i]);
            }
        }
        free(immediate);
    } else if (strcmp(option, "-lg") == 0) {
        // List sibling processes
        pid_t *siblings;
        int count;
        get_siblings(process_id, &siblings, &count);

        if (count == 0) {
            printf("No sibling/s\n");
        } else {
            for (int i = 0; i < count; i++) {
                printf("%d\n", siblings[i]);
            }
        }
        free(siblings);
    } else if (strcmp(option, "-lz") == 0) {
        // List defunct sibling processes
        pid_t *defunct_siblings;
        int count;
        get_defunct_siblings(process_id, &defunct_siblings, &count);

        if (count == 0) {
            printf("No defunct sibling/s\n");
        } else {
            for (int i = 0; i < count; i++) {
                printf("%d\n", defunct_siblings[i]);
            }
        }
        free(defunct_siblings);
    } else if (strcmp(option, "-df") == 0) {
        // List defunct descendants
        pid_t *defunct;
        int count;
        get_defunct_descendants(process_id, &defunct, &count);

        if (count == 0) {
            printf("No descendant zombie process/es\n");
        } else {
            for (int i = 0; i < count; i++) {
                printf("%d\n", defunct[i]);
            }
        }
        free(defunct);
    } else if (strcmp(option, "-gc") == 0) {
        // List grandchildren
        pid_t *grandchildren;
        int count;
        get_grandchildren(process_id, &grandchildren, &count);

        if (count == 0) {
            printf("No grandchildren\n");
        } else {
            for (int i = 0; i < count; i++) {
                printf("%d\n", grandchildren[i]);
            }
        }
        free(grandchildren);
    } else if (strcmp(option, "-do") == 0) {
        // Print status of process_id
        printf("%s\n", is_defunct(process_id) ? "Defunct" : "Not defunct");
    } else if (strcmp(option, "--pz") == 0) {
        // Kill parents of zombie processes
        kill_parents_of_zombies(process_id);
        printf("Parents of zombie processes that are descendants of %d have been killed\n", process_id);
    } else if (strcmp(option, "-sk") == 0) {
        // Kill all descendants with SIGKILL
        pid_t *descendants;
        int count;
        get_descendants(process_id, &descendants, &count);

        for (int i = 0; i < count; i++) {
            kill(descendants[i], SIGKILL);
        }
        printf("All descendants of %d have been killed\n", process_id);
        free(descendants);
    } else if (strcmp(option, "-st") == 0) {
        // Stop all descendants with SIGSTOP
        pid_t *descendants;
        int count;
        get_descendants(process_id, &descendants, &count);

        for (int i = 0; i < count; i++) {
            kill(descendants[i], SIGSTOP);
        }
        printf("All descendants of %d have been stopped\n", process_id);
        free(descendants);
    } else if (strcmp(option, "-dt") == 0) {
        // Continue all stopped descendants with SIGCONT
        pid_t *descendants;
        int count;
        get_descendants(process_id, &descendants, &count);

        for (int i = 0; i < count; i++) {
            kill(descendants[i], SIGCONT);
        }
        printf("All stopped descendants of %d have been continued\n", process_id);
        free(descendants);
    } else if (strcmp(option, "-rp") == 0) {
        // Kill root_process with SIGKILL
      kill(root_pid, SIGKILL);
      printf("Root process %d has been killed\n", root_pid);
    } else {
        printf("Invalid option: %s\n", option);
    }
}

// Function to create the process tree
void create_process_tree() {
    // Level 1 - First child
    child1_pid = fork();

    if (child1_pid < 0) {
        perror("Fork failed");
        exit(1);
    }

    if (child1_pid == 0) { // Child 1
        print_process_info("Child 1");
         printf("Child 1: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

        // Level 2 - First grandchild
        grandchild1_pid = fork();

        if (grandchild1_pid < 0) {
            perror("Fork failed");
            exit(1);
        }

        if (grandchild1_pid == 0) { // Grandchild 1
            print_process_info("Grandchild 1");
              printf("Grandchild 1: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

            // Create a zombie process under Grandchild 1
            zombie1_pid = create_zombie();

            // Level 3 - First great-grandchild
            greatgrandchild1_pid = fork();

            if (greatgrandchild1_pid < 0) {
                perror("Fork failed");
                exit(1);
            }

            if (greatgrandchild1_pid == 0) { // Great-grandchild 1
                print_process_info("Great-grandchild 1");
                  printf("Great-grandchild 1: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

                while (keep_running) {
                    sleep(5); // Increased sleep time
                }
                exit(0);
            }

            while (keep_running) {
                sleep(5); // Increased sleep time
            }
            kill(greatgrandchild1_pid, SIGTERM);
            waitpid(greatgrandchild1_pid, NULL, 0);
            exit(0);
        }

        // Level 2 - Second grandchild
        grandchild2_pid = fork();

        if (grandchild2_pid < 0) {
            perror("Fork failed");
            exit(1);
        }

        if (grandchild2_pid == 0) { // Grandchild 2
            print_process_info("Grandchild 2");
               printf("Grandchild 2: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

            // Create a zombie process under Grandchild 2
            zombie2_pid = create_zombie();

            while (keep_running) {
                sleep(5); // Increased sleep time
            }
            exit(0);
        }

        while (keep_running) {
            sleep(5); // Increased sleep time
        }
        kill(grandchild1_pid, SIGTERM);
        kill(grandchild2_pid, SIGTERM);
        waitpid(grandchild1_pid, NULL, 0);
        waitpid(grandchild2_pid, NULL, 0);
        exit(0);
    }

    // Level 1 - Second child
    child2_pid = fork();

    if (child2_pid < 0) {
        perror("Fork failed");
        exit(1);
    }

    if (child2_pid == 0) { // Child 2
        print_process_info("Child 2");
               printf("Child 2: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

        // Level 2 - Third grandchild
        grandchild3_pid = fork();

        if (grandchild3_pid < 0) {
            perror("Fork failed");
            exit(1);
        }

        if (grandchild3_pid == 0) { // Grandchild 3
            print_process_info("Grandchild 3");
               printf("Grandchild 3: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

            // Create a zombie process under Grandchild 3
            zombie3_pid = create_zombie();

            while (keep_running) {
                sleep(5); // Increased sleep time
            }
            exit(0);
        }

        // Level 2 - Fourth grandchild
        grandchild4_pid = fork();

        if (grandchild4_pid < 0) {
            perror("Fork failed");
            exit(1);
        }

        if (grandchild4_pid == 0) { // Grandchild 4
            print_process_info("Grandchild 4");
               printf("Grandchild 4: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

            // Level 3 - Second great-grandchild
            greatgrandchild2_pid = fork();

            if (greatgrandchild2_pid < 0) {
                perror("Fork failed");
                exit(1);
            }

            if (greatgrandchild2_pid == 0) { // Great-grandchild 2
                print_process_info("Great-grandchild 2");
                    printf("Great-grandchild 2: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID

                while (keep_running) {
                    sleep(5); // Increased sleep time
                }
                exit(0);
            }

            while (keep_running) {
                sleep(5); // Increased sleep time
            }
            kill(greatgrandchild2_pid, SIGTERM);
            waitpid(greatgrandchild2_pid, NULL, 0);
            exit(0);
        }

        while (keep_running) {
            sleep(5); // Increased sleep time
        }
        kill(grandchild3_pid, SIGTERM);
        kill(grandchild4_pid, SIGTERM);
        waitpid(grandchild3_pid, NULL, 0);
        waitpid(grandchild4_pid, NULL, 0);
        exit(0);
    }
}

// Function to display the menu
void display_menu() {
    printf("\n===== Process Tree Menu =====\n");
    printf("1. Show process tree information\n");
    printf("2. Run prct command\n");
    printf("3. Exit\n");
    printf("Enter your choice: ");
}

// Function for command line interaction
void *cli_thread(void *arg) {
    char command[1024];
    int choice;

    while (keep_running) {
        display_menu();
        scanf("%d", &choice);
        getchar(); // Consume the newline

        switch (choice) {
            case 1: {
                // Show process tree information
                printf("\nProcess Tree Information:\n");
                printf("Root PID: %d\n", root_pid);

                if (process_exists(child1_pid)) {
                    printf("Child 1 PID: %d\n", child1_pid);
                } else {
                    printf("Child 1 has terminated\n");
                }

                if (process_exists(child2_pid)) {
                    printf("Child 2 PID: %d\n", child2_pid);
                } else {
                    printf("Child 2 has terminated\n");
                }

                if (process_exists(grandchild1_pid)) {
                    printf("Grandchild 1 PID: %d (has zombie child)\n", grandchild1_pid);
                } else {
                    printf("Grandchild 1 has terminated\n");
                }

                if (process_exists(grandchild2_pid)) {
                    printf("Grandchild 2 PID: %d (has zombie child)\n", grandchild2_pid);
                } else {
                    printf("Grandchild 2 has terminated\n");
                }

                if (process_exists(grandchild3_pid)) {
                    printf("Grandchild 3 PID: %d (has zombie child)\n", grandchild3_pid);
                } else {
                    printf("Grandchild 3 has terminated\n");
                }

                if (process_exists(grandchild4_pid)) {
                    printf("Grandchild 4 PID: %d\n", grandchild4_pid);
                } else {
                    printf("Grandchild 4 has terminated\n");
                }

                printf("Great-grandchild 1 PID: Process under Grandchild 1\n");
                printf("Great-grandchild 2 PID: Process under Grandchild 4\n");

                printf("\nProcess Tree Structure:\n");
                printf("Root (PID: %d)\n", root_pid);
                printf("|-- Child 1 (PID: %d)\n", child1_pid);
                printf("|   |-- Grandchild 1 (PID: %d)\n", grandchild1_pid);
                printf("|   |   |-- Zombie Process\n");
                printf("|   |   |-- Great-grandchild 1\n");
                printf("|   |-- Grandchild 2 (PID: %d)\n", grandchild2_pid);
                printf("|       |-- Zombie Process\n");
                printf("|-- Child 2 (PID: %d)\n", child2_pid);
                printf("    |-- Grandchild 3 (PID: %d)\n", grandchild3_pid);
                printf("    |   |-- Zombie Process\n");
                printf("    |-- Grandchild 4 (PID: %d)\n", grandchild4_pid);
                printf("        |-- Great-grandchild 2\n");
                break;
            }

            case 2: {
                // Run prct command
                pid_t root_pid_input, process_id_input;
                char option_input[10];

                printf("\nEnter prct command (format: prct root_process process_id option): ");
                if (scanf("prct %d %d %9s", &root_pid_input, &process_id_input, option_input) == 3) {
                    // Create a temporary argv array to pass to handle_prct_command
                    char root_pid_str[20], process_id_str[20];

                    // Convert the integers to strings
                    snprintf(root_pid_str, sizeof(root_pid_str), "%d", root_pid_input);
                    snprintf(process_id_str, sizeof(process_id_str), "%d", process_id_input);

                    char *prct_argv[] = {root_pid_str, process_id_str, option_input, NULL};

                    handle_prct_command(3, prct_argv);
                } else {
                    fprintf(stderr, "Invalid command format. Use: prct root_process process_id option\n");
                     while (getchar() != '\n'); // Clear input buffer

                }
                break;
            }

            case 3:
                // Exit
                printf("Exiting...\n");
                keep_running = 0;
                break;

            default:
                printf("Invalid choice, please try again.\n");
        }
    }

    return NULL;
}

int main() {
    pthread_t tid;

    // Set up signal handler
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);

    // Store root process PID
    root_pid = getpid();

    printf("\n=== Process Tree Creator and Tracker ===\n");
    printf("This program creates a process tree and allows you to run prct commands on it.\n");
    printf("Root process PID: %d\n\n", root_pid);

    // Create the process tree
    create_process_tree();

    // Store child PIDs immediately after fork:
    sleep(1); // Let processes stabilize. Important!
        printf("Root Process: PID: %d, PPID: %d\n", getpid(), getppid()); // Display PID and PPID


    // Create a thread for command line interaction
    pthread_create(&tid, NULL, cli_thread, NULL);

    // Wait for user to exit
    while (keep_running) {
        sleep(1);
    }

    // Clean up
    printf("\nTerminating all processes...\n");
    kill(child1_pid, SIGTERM);
    kill(child2_pid, SIGTERM);
    waitpid(child1_pid, NULL, 0);
    waitpid(child2_pid, NULL, 0);

    printf("All processes terminated.\n");

    return 0;
}