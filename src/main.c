#include <stdio.h>
#include <fcntl.h> //file control for open function. 
#include <unistd.h> //unix standard for close function
#include <inttypes.h>
#include <sys/stat.h>
#include <pthread.h> //for the threads compiling
#include <stdlib.h> //added for module 2
#include <string.h> //added for module 2

typedef enum {
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    FAILED
} transferStatus; // pending =0,in prpgress = 1, completed=2, failed=3. allows the number constants to have names and allows to have a defined
//set of integer constants. 

enum { MAX_WORKERS = 4 };

typedef struct{
    const char *source_path;
    const char *destination_path;
    transferStatus status;
    off_t source_size; //size of original file
    uintmax_t bytes_transferred; //stores how many bytes have been transferred. uses uintmax as files can be very large
    int result; //stores wheter the copy succeeded or failed. 0-succeed, 1-failed
} TransferTask;

static int copy_bytes(int source_fd, int destination_fd, TransferTask *task){      //added transfertask     //static makes the function local 
//to main function. 
    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read=read(source_fd, buffer, sizeof buffer))>0){
        ssize_t bytes_written=0; 
        while(bytes_written<bytes_read){
            ssize_t result= write(destination_fd, buffer+bytes_written, (size_t)(bytes_read-bytes_written));
            if(result==-1){
                perror("count not write to destination");
                return -1;
            }
            if(result==0){
                fprintf(stderr, " write made no progress");
                return -1;
            }
            bytes_written+=result;
            task->bytes_transferred += (uintmax_t)result; //updates the count of total bytes transferred/copied so far. 
        }
    }
    if(bytes_read==-1){
        perror("could not read source");
        return -1;
    }
    return 0;
}

static int copy_file(TransferTask *task){//entire structure is entered,

    const char *source_path=task->source_path;
    const char *destination_path=task->destination_path;
    struct stat source_info;
    if(stat(source_path, &source_info)==-1){
        perror(source_path);
        return -1;
    }
    if(!S_ISREG(source_info.st_mode)){
        fprintf(stderr, "%s is not a regular file\n", source_path);
        return -1;
    }

    task->source_size = source_info.st_size;
    int source_fd=open(source_path, O_RDONLY);
   if(source_fd==-1){//means if it doesnt work and returns -1 for some error
    perror(source_path);
    return -1;
   }

    int destination_fd=open(destination_path, O_WRONLY|O_CREAT|O_EXCL, 0600);

    if(destination_fd==-1){
        perror(destination_path);
        close(source_fd);
        return -1;
    }
    int copy_result=copy_bytes(source_fd, destination_fd, task); //task
    if(close(source_fd)==-1){                       //if the file could not be closed, and hence it prints -1. then
        perror("could not close source");
        copy_result=-1;
    }
    if(close(destination_fd)==-1){
        perror("could not close destination");
        copy_result=-1;
    }
    if(copy_result==-1){
        if (unlink(destination_path)==-1) {
            perror("could not remove incomplete destination");
        }
        return -1;
    }
    printf("copied %s to %s (and there are %jd bytes in source at start)\n",
           source_path, destination_path, (intmax_t)source_info.st_size);
    return 0;
}





//keeps track of all the files that need to be copied. 
typedef struct {
    TransferTask *tasks; //full array list of all transfer jobs
    int task_count; //total numbers of files to copy
    int next_task; //tracks which files havent been claimed yet. 
    pthread_mutex_t mutex; //mutex. 
} taskList;

//this is just a safety function for threads. standard thread functions like locking or unlocking a mutex only returns an error when they 
//fail. this function checks if anyting went wrong and if it did, it prints a readable error message stderr(error) explaining what happens. 
static void check_pthread(int error, const char *work){
    if(error!=0){
         fprintf(stderr ,"%s: %s\n", work, strerror(error));
        exit(EXIT_FAILURE); //immediately exits after. 
    }
}

//important. learn properly
static void *worker(void *argument)
{
    taskList *list=argument;
    while(1){
        check_pthread(pthread_mutex_lock(&list->mutex) ,"lock the task list"); //the thread calls the 
        if (list->next_task==list->task_count){ //checks if all the files have already been claimed??. if yes, unlocks mutex. 
            check_pthread(pthread_mutex_unlock(&list->mutex),"unlock thetask list"); //unlock mutex. 
            return NULL;
        }
        TransferTask *task =&list->tasks[list->next_task];
        list->next_task++; //takes the point to the current thread and points (increments it) it to the next thread
        check_pthread(pthread_mutex_unlock(&list->mutex) ,"unlock the task list"); //calls it so that the other threads can get their work
        task->status = IN_PROGRESS;
        task->status = copy_file(task) == 0 ? COMPLETED : FAILED;
        //task->result=copy_file(task->source_path, task->destination_path); calls this so that multiple files can be copied parallely. stores outcome in task->result. ???
    }
}

static const char *status_name(transferStatus status)
{
    switch(status){
    case PENDING:
        return "Pending";
    case IN_PROGRESS: 
        return "In progress";
    case COMPLETED:   
        return "Completed";
    case FAILED:      
        return "Failed";
    }
    return "Unknown";
}

static int print_report(const taskList *list, int started){
    int succeeded=0;
    int failed=0;
    uintmax_t total_bytes=0;
    printf("Transfer report \n");
    for(int i=0;i<list->task_count;i++){
        const TransferTask *task=&list->tasks[i];
        printf("%s -> %s |%s|source size:  ", task->source_path, task->destination_path,status_name(task->status));
        if (task->source_size<0){ //incase source size is -neg (file size checking failed)
            printf("unknown");
        }else{
            printf("%jd bytes",(intmax_t)task->source_size);
        }
        printf(" | transferred: %ju bytes\n",task->bytes_transferred);
        total_bytes+=task->bytes_transferred;
        if(task->status==COMPLETED){
            succeeded++;
        }else if(task->status==FAILED){
            failed++;
        }
    }
    printf("Summary:%d files |%d succeeded|%d failed| %ju bytes transferred | %d workers started\n", list->task_count, succeeded, failed, total_bytes, started);
    return succeeded==list->task_count ? 0:1;
}

//argc counts the number of entries. arg[] reads everything printed on that line. argv[1] is the source file. argv[2] is the destination file
int main(int argc, char *argv[])
{
    int count_worker=MAX_WORKERS;
    int first_path=1;
    if(argc>1&&strcmp(argv[1], "-j")==0){
        if (argc<3||strlen(argv[2])!=1 || argv[2][0]<'1'||argv[2][0]>'4'){
            fprintf(stderr, "-j needs a worker count from 1 to 4\n");
            return 1;
        }
        count_worker=argv[2][0]-'0';
        first_path=3;
    }
    int path_count=argc-first_path;
    if(path_count<2||path_count% 2!=0){
        fprintf(stderr, "the usage: %s [-j 1-4] source destination [SOURCE DESTINATION ...]\n",
                argv[0]);
        return 1;
    }
    taskList list={.task_count=path_count/2 };
    list.tasks = calloc((size_t)list.task_count, sizeof *list.tasks);
    if(list.tasks==NULL){
        perror("allocates the task list");
        return 1;
    }
    for(int i=0; i<list.task_count;i++){
        list.tasks[i].source_path=argv[first_path+2*i];
        list.tasks[i].destination_path=argv[first_path+2*i+1];
        list.tasks[i].status=PENDING;
        list.tasks[i].source_size=-1;
    }
    check_pthread(pthread_mutex_init(&list.mutex, NULL),"initialize the  mutex");

    pthread_t threads[MAX_WORKERS]; //creates an array that is capable of storing 4 thread identifiers. 
    int started=0;
    while(started<count_worker){
        int error=pthread_create(&threads[started], NULL, worker, &list);
        if (error!=0){
            fprintf(stderr, "could not create a worker %d: %s\n",
                    started+1,strerror(error));
            break;
        }
        started++;
    }

    if(started==0){
        fprintf(stderr,"the main thread will process the tasks\n"); 
        worker(&list);
    }
    for(int i=0;i<started;i++){
        check_pthread(pthread_join(threads[i], NULL), "join the workers");
    }
    int exit_status=print_report(&list, started);
    check_pthread(pthread_mutex_destroy(&list.mutex), "destroy the mutex"); //since we dont need it anymore
    free(list.tasks);
    return exit_status;
}
